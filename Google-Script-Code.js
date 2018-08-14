///////////////////////////////////////////
var TIME_ADJUST   = 5;    // Minus this from current time to find first reading time
var READ_INTERVAL = 30;   // Interval between readings during monitored hours
var NUM_READINGS  = 10;   // Number of readings before sending
var BATTERY_MODE  = 1;    // 1 = On Battery, 0 = Off Battery
var FACTORY_RESET = 0;    // Set to 1 to tell device to factory reset

var DEVICE_MAC    = 'MA:CA:DD:RE:SS';
var DATA_SHEET    = 'RAW';
var VOLTAGE       = 230;
//var STATS_SHEET   = 'STATS';

var EK_USERNAME = 'email@example.com';
var EK_PASSWORD = 'mypassword';

var scriptProperties = PropertiesService.getScriptProperties();
load_data();
///////////////////////////////////////////

var SHEETS = SpreadsheetApp.getActiveSpreadsheet();
  
function doGet(e) {
  var resp = process(e.parameter.s, parseInt(e.parameter.i), e.parameter.r.split('-'));
  return ContentService.createTextOutput(resp);
}

function doPost(e) {
  var data = JSON.parse(e.postData.contents);
  var resp = process(data['m'], data['i'], data['r']);
  return ContentService.createTextOutput(resp);
}

function test() {
  var resp = process(DEVICE_MAC, 5, [100,20,30,40,1000,600,70,80,90,100]);
  Logger.log(resp);
  
  nightly();
  Logger.log('done');
}

function process(mac, interval, readings) {
  var data_sheet = SHEETS.getSheetByName(DATA_SHEET);
  
  if (mac != DEVICE_MAC) {
    Utilities.sleep(2000);
    return '';
  }
  
  var datetime      = new Date();
  var minus_seconds = (interval * readings.length) + TIME_ADJUST;
  var row_index     = data_sheet.getLastRow() + 1;
  var rowData       = [];

  datetime.setSeconds(datetime.getSeconds() - minus_seconds);
  
  for (var index in readings) {
    rowData[0] = datetime;
    rowData[1] = (readings[index]/100)*VOLTAGE;
    
    data_sheet.getRange(row_index, 1, 1, 2).setValues([rowData]);
    
    row_index += 1;
    datetime.setSeconds(datetime.getSeconds() + interval);
  }
  
  var config = {
    'i': READ_INTERVAL,
    'n': NUM_READINGS,
    'b': BATTERY_MODE,
    'f': FACTORY_RESET,
  };

  if (BATTERY_MODE == 1) {
    for (var index in this.scriptData['sleep_times']) {
      var start  = new Date(this.scriptData['sleep_times'][index][0]);
      var end    = new Date(this.scriptData['sleep_times'][index][1]);

      if (datetime > start && datetime < end) {
        config['s'] = parseInt((end - datetime) / 1000);
        break;
      }
    }
  }

  return JSON.stringify(config)+"\n";
}

function login() {
  var payload = {
    'LoginForm[username]': EK_USERNAME,
    'LoginForm[password]': EK_PASSWORD,
  };
  
  var options = {'method':'post', 'payload':payload, 'followRedirects':false};
  var resp = UrlFetchApp.fetch('https://www.electrickiwi.co.nz/Site/login', options);
  var headers = resp.getAllHeaders();
  if (headers['Location'] == null) {
    return false;
  }
  
  var cookies = headers["Set-Cookie"];
  var cookie_str = '';
  
  for (var i = 0; i < cookies.length; i++) {
    cookie_str += cookies[i].split(';')[0] + ';';
  };
  
  return cookie_str;
}

function convertTo24Hour(time) {
    var hours = parseInt(time.substr(0, 2));
    if(time.indexOf('AM') != -1 && hours == 12) {
        time = time.replace('12', '0');
    }
    if(time.indexOf('PM')  != -1 && hours < 12) {
        time = time.replace(hours, (hours + 12));
    }
    return time.replace(/(AM|PM)/, '').trim();
}

function update_hours(cookies) {
  var options = {'method':'get', 'headers':{'Cookie':cookies}};
  var html = UrlFetchApp.fetch('https://www.electrickiwi.co.nz/account/update-hour-of-power', options).getContentText();
  
  var regExp = new RegExp("KiwikPayment_free_hour_consumption([^]*?)</select>", "im");
  html = regExp.exec(html)[1];
  
  this.scriptData['hours'] = [];
  this.scriptData['sleep_times'] = [];
  
  var result;
  regExp = new RegExp("<option.*?value=\"([0-9]+)\".*?>(.*?)</option>", "gi");  
  while ((result = regExp.exec(html)) !== null) {
    this.scriptData['hours'].push([convertTo24Hour(result[2]), result[1]]);
  }
  
  var now = new Date();
  
  for (var index in this.scriptData['hours']) {
    index = parseInt(index);
    
    var hour      = this.scriptData['hours'][index][0].split(':');
    var next_hour = (this.scriptData['hours'][parseInt(index)+1] || this.scriptData['hours'][0])[0].split(':');
    
    var start = new Date(now.getFullYear(), now.getMonth(), now.getDate(), parseInt(hour[0])+1, hour[1] || 0, hour[2] || 0);
    var end   = new Date(now.getFullYear(), now.getMonth(), now.getDate(), next_hour[0], next_hour[1] || 0, next_hour[2] || 0);
    
    if (end < now) {
      start.setDate(start.getDate() + 1);
      end.setDate(end.getDate() + 1);
    }

    if (end > start) {
      this.scriptData['sleep_times'].push([start, end]);
    }
  }
  
  save_data();
  return this.scriptData['hours'];
}

function load_data() {
  try{
    this.scriptData = JSON.parse(scriptProperties.getProperty('data')) || {};
  }
  catch(err) {
    this.scriptData = {};
  }
}

function save_data() {
  scriptProperties.setProperty('data', JSON.stringify(this.scriptData));
}

function set_hour(cookies, hour_index) {
  var payload = {
    'KiwikPayment[free_hour_consumption]': hour_index,
  };
  
  var options = {'method':'post', 'payload':payload, 'headers':{'Cookie':cookies}, 'followRedirects':false};
  var resp = UrlFetchApp.fetch('https://www.electrickiwi.co.nz/account/update-hour-of-power', options);
  return resp.getAllHeaders()['Location'] != null;
}

function get_highest_hour() {
  var data_sheet = SHEETS.getSheetByName(DATA_SHEET);
  
  var now = new Date();
  var min = new Date(now.getFullYear(), now.getMonth(), now.getDate(), 0, 0, 0);
  
  var values = data_sheet.getDataRange().getValues();
  var hours  = this.scriptData['hours'].slice();
  
  data_sheet.clear({formatOnly: true, contentsOnly: true});
  
  for(n=0;n<hours.length;++n){
    var hour = hours[n][0].split(':');
    
    var start = new Date(now.getFullYear(), now.getMonth(), now.getDate(), hour[0], hour[1] || 0, hour[2] || 0);
    var end   = new Date(now.getFullYear(), now.getMonth(), now.getDate(), parseInt(hour[0])+1, hour[1] || 0, hour[2] || 0);
    
    hours[n][2] = start;
    hours[n][3] = end;
    hours[n][4] = [];
  }
  
  for(n=0;n<values.length;++n){
    var date    = values[n][0];
    var reading = values[n][1];
    if (date == "" || date < min || date > now) {
      continue;
    }
    
    for(i=0;i<hours.length;++i){
      if (date > hours[i][2] && date < hours[i][3]) {
        hours[i][4].push([date, reading]);
      }
      
      if (date < hours[i][2]) {
        break;
      }
    }
  }
  
  var highest = [null, 0];
  for(i=0;i<hours.length;++i){
    var readings = hours[i][4];
    var total_ws = 0;
    var total_t  = 0;
        
    for(r=1;r<readings.length;++r){
      var dt = (readings[r][0] - readings[r-1][0]) / 1000;
      var average = (readings[r][1] + readings[r-1][1]) / 2;
      total_ws += average * dt;
      total_t  += dt;
    }
    
    var watts = +(total_ws/total_t).toFixed(3) || 0;
    hours[i][4] = watts;
    if (watts > highest[1]) {
      highest = [hours[i][1], watts];
    }
  }
  
  return highest[0];
}

function minutely() {
  var now = new Date();
  
  if ((now.getHours() != 23) || (now.getMinutes() < 50)) {
    if (this.scriptData['has_run']) {
      this.scriptData['has_run'] = false;
      save_data();
    }
    return;
  }
  else if (this.scriptData['has_run']) {
    return;
  }
  
  this.scriptData['has_run'] = true;
  save_data();
  
  nightly();
}

function nightly() {
  var cookies = login();
  
  update_hours(cookies);
  
  var highest_hour = get_highest_hour();
  if (highest_hour) {
    set_hour(cookies, highest_hour);
    //Notifcation?
  }
}