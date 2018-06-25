///////////////////////////////////////////
var TIME_ADJUST   = 5;    // Minus this from current time to find first reading time
var READ_INTERVAL = 30;   // Interval between readings during monitored hours
var NUM_READINGS  = 10;   // Number of readings before sending
var BATTERY_MODE  = 1;    // 1 = On Battery, 0 = Off Battery
var FACTORY_RESET = 0;    // Set to 1 to tell device to factory reset

var DEVICE_MAC    = 'MA:CA:DD:RE:SS';
var DATA_SHEET    = 'RAW';
var STATS_SHEET   = 'STATS';
var SLEEP_TIMES   = [['07:00','09:00'], ['17:00','21:00']]

var FREE_HOURS    = ['00:00', '00:30', '01:00', '01:30', '02:00', '02:30', '03:00', '03:30', '04:00', '04:30', '05:00', '05:30', '06:00', 
                    '09:00', '09:30', '10:00', '10:30', '11:00', '11:30', '12:00', '12:30', '13:00', '13:30', '14:00', '14:30', '15:00', '15:30', '16:00',
                    '21:00', '21:30', '22:00', '22:30', '23:00']
///////////////////////////////////////////

var SHEETS = SpreadsheetApp.getActiveSpreadsheet();
  
function doPost(e) {
  var data = JSON.parse(e.postData.contents);
  var resp = process(data['m'], data['i'], data['r']);
  return ContentService.createTextOutput(resp);
}

function test() {
  var resp = process(DEVICE_MAC, READ_INTERVAL, [10,20,30,40,50,60,70,80,90,100]);
  Logger.log(resp);
}

function process(mac, interval, readings) {
  if (mac != DEVICE_MAC) {
    Utilities.sleep(2000);
    return '';
  }

  DATA_SHEET = SHEETS.getSheetByName(DATA_SHEET);
  
  var datetime      = new Date();
  var minus_seconds = (interval * readings.length) + TIME_ADJUST;
  var row_index     = DATA_SHEET.getLastRow() + 1;
  var rowData       = [];

  datetime.setSeconds(datetime.getSeconds() - minus_seconds);
  
  for (var index in readings) {
    rowData[0] = datetime;
    rowData[1] = (readings[index]/100);
    
    DATA_SHEET.getRange(row_index, 1, 1, rowData.length).setValues([rowData]);
    
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
    for (var index in SLEEP_TIMES) {
      var start  = SLEEP_TIMES[index][0].split(':');
      var end    = SLEEP_TIMES[index][1].split(':');
      start      = new Date(datetime.getFullYear(), datetime.getMonth(), datetime.getDate(), start[0], start[1] || 0, start[2] || 0);
      end        = new Date(datetime.getFullYear(), datetime.getMonth(), datetime.getDate(), end[0], end[1] || 0, end[2] || 0);
      
      if (end < start) {
        end.setDate(end.getDate() + 1);
      }
      
      if (datetime > start && datetime < end) {
        config['s'] = parseInt((end - datetime) / 1000);
        break;
      }
    }
  }

  return JSON.stringify(config)+"\n";
}