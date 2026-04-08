
function truncate(str, max) {
  if (!str) return "";
  var commaIndex = str.indexOf(",");
  if (commaIndex !== -1) {
    str=str.substring(0, commaIndex);
  }
  return str.length > max ? str.substring(0, max - 3) + "..." : str;
}

const REFRESH_INTERVAL = 4 * 60 * 60 * 1000; // 4 hours

const TRACK_URL="https://api.pkge.net/v1/packages";
var TRACK_API_KEY=getExtra(16,1);

function getSpreadsheet() {

  const files = DriveApp.getFilesByName("ESP32Dashboard");

  if (!files.hasNext()) {
    throw new Error("Spreadsheet not found");
  }

  const file = files.next();
  return SpreadsheetApp.open(file);
}

function getLatestStatusFromResponse(responseData) {
  // responseData: parsed JSON from the API
  // results[0].data.events is the array of events for the first tracking
  const results = responseData.results || [];

  let latestInfo = [];

  results.forEach(trackingItem => {
    const events = trackingItem.data?.events || [];
    if (events.length === 0) {
      latestInfo.push({
        trackingNumber: trackingItem.trackingNumber,
        status: trackingItem.data?.deliveryStatus || "Unknown",
        lastEvent: null
      });
      return;
    }

    // Sort events by timestamp descending
    events.sort((a, b) => new Date(b.timestamp) - new Date(a.timestamp));

    const latestEvent = events[0];

    latestInfo.push({
      trackingNumber: trackingItem.trackingNumber,
      carrier: trackingItem.carrier,
      deliveryStatus: trackingItem.data?.deliveryStatus || "Unknown",
      lastEvent: {
        timestamp: latestEvent.timestamp,
        status: latestEvent.status,
        description: latestEvent.description,
        location: latestEvent.location
      }
    });
  });

  return latestInfo;
}

function getMakerWorldStats() {
  const ss = getSpreadsheet();
  const sheet = ss.getSheetByName("Layout");
  const data = sheet.getDataRange().getValues();

  // Helper function to get column values for row where column A = 64
  function getExtra(matchValue, colIndex) {
    const row = data.find(r => r[0] === matchValue); // column A = index 0
    return row ? row[colIndex] : null;
  }

  const designId = getExtra(64, 12); // Column M (13th column, index 12)
  const limit = getExtra(64, 13);    // Column N (14th column, index 13)

  if (!designId || !limit) {
    Logger.log("No row found with column A = 64 or missing designId/limit");
    return null;
  }

  const url = `https://makerworld.com/api/v1/design-service/published/${designId}/design?limit=${limit}`;
  // Logger.log("Fetching URL: " + url);

  const response = UrlFetchApp.fetch(url);
  const json = JSON.parse(response.getContentText());

  let totalLikes = 0;
  let totalDownloads = 0;
  let totalPrints = 0;
  let totalCollections = 0;
  let totalBoosts = 0;

  json.hits.forEach(item => {
    totalLikes += item.likeCount || 0;
    totalDownloads += item.downloadCount || 0;
    totalPrints += item.printCount || 0;
    totalCollections += item.collectionCount || 0;
    totalBoosts += item.boostCnt || 0;
  });

  const results = {
    designId: designId,
    limit: limit,
    total: {
      likes: Math.floor(totalLikes),
      downloads: Math.floor(totalDownloads),
      prints: Math.floor(totalPrints),
      collections: Math.floor(totalCollections),
      boosts: Math.floor(totalBoosts)
    },
    lastUpdated: new Date().toISOString()
  };

  // Logger.log(results);
  return results;
}


function getStockInfo() {
  const ss = getSpreadsheet();
  const sheet = ss.getSheetByName("stocks");
  // var sheet = SpreadsheetApp.openById(spreadsheetId).getSheetByName(sheetName);
  
  // Get all data starting from the second row
  var lastRow = sheet.getLastRow();
  var data = sheet.getRange(2, 1, lastRow - 1, 11).getValues(); 
  // 2 = start row, 1 = start column, 6 columns (name, price, dayHigh, dayLow, 52wHigh, 52wLow)

  var jsonArray = data.map(function(row) {
  const averagePrice = row[8]; // column I
  const currentPrice = row[1]; // column B

  let priceChangePer = null;
  if (averagePrice && averagePrice > 0) {
    priceChangePer = ((currentPrice - averagePrice) / averagePrice) * 100;
  }

  return {
    name: row[0].split(':')[1],        // column A
    price: currentPrice,               // column B
    dayHigh: row[2],                    // column C
    dayLow: row[3],                     // column D
    fiftyTwoWeekHigh: row[4],           // column E
    fiftyTwoWeekLow: row[5],            // column F
    prevClose: row[6],                  // column G
    opening: row[7],                     // column H
    averagePrice: averagePrice,         // column I
    priceChangePer: priceChangePer,      // calculated percentage
    total: row[10]
  };
});

  return jsonArray
}


function getLayoutData() {
  const ss = getSpreadsheet(); // Use your existing getSpreadsheet() function
  const sheet = ss.getSheetByName("Layout"); // Replace "Layout" with your sheet name

  if (!sheet) {
    throw new Error("Layout sheet not found");
  }

  const data = sheet.getDataRange().getValues();
  const headers = data[0]; // First row as headers
  let result = [];

  for (let i = 1; i < data.length; i++) {
    const row = data[i];
    if (!row[0]) continue; // Skip empty rows (no ID)

    let rowObj = {};
    headers.forEach((header, index) => {
      rowObj[header] = row[index];
    });
    result.push(rowObj);
  }

  return result;
}

function getCalendar()
{
  const cal = CalendarApp.getDefaultCalendar();
  const now = new Date();

  const next24h = new Date(now.getTime() + 24 * 60 * 60 * 1000);
  console.log( now );
  console.log( next24h );
  const upcoming = cal.getEvents(now, next24h);

  const next24hEvents = upcoming.map(ev => ({
    title: ev.getTitle(),
    start: ev.getStartTime().toISOString(),
    end: ev.getEndTime().toISOString(),
    allDay: ev.isAllDayEvent()
  }));

  const year = now.getFullYear();
  const month = now.getMonth();
  const todayStart = new Date(year, month, now.getDate());
  const monthEnd = new Date(year, month + 1, 1);

  const monthEvents = cal.getEvents(todayStart, monthEnd);
  const daysSet = new Set();

  monthEvents.forEach(ev => {
    const d = ev.getStartTime().getDate();
    if (d >= now.getDate()) daysSet.add(d);
  });

  const payload = {
    next24hEvents,
    daysWithEvents: [...daysSet].sort((a, b) => a - b)
  };
  return payload;
}

function getAllInfo() {
  const cache = CacheService.getScriptCache();
  // Try cache first
  const cached = cache.get("storedData");

  if (cached) return JSON.parse(cached);

  const stocks = getStockInfo();
  const tracking = getTrackingInfo();
  const layout = getLayoutData();
  const calEvents = getCalendar();
  const maker = getMakerWorldStats(); // optional
  // --- Open-Meteo weather ---
  const latitude = 45.5017;   // Montreal, replace with dynamic if needed
  const longitude = -73.5673;
  const weatherDays = 4;       // number of forecast days
  const weather = fetchOpenMeteo(latitude, longitude, weatherDays, 3);

// Logger.log( weather  );


  const results = {
    calEvents: calEvents,
    stocks: stocks,
    tracking: tracking,
    layout: layout,
    makerworld: maker,
    weather: weather,
    lastUpdated: new Date().toISOString()
  };
  // Cache for 15 minutes (900 sec)
  cache.put("storedData", JSON.stringify(results), 900);
  return results;
}


/**
 * Flexible GET endpoint:
 * - No query param → return all cached info
 * - ?data=weather → return only weather
 * - ?data=stocks → return only stocks, etc.
 */
function doGet(e) {

  if (e && e.parameter && e.parameter.fn )
  {
    const cache = CacheService.getScriptCache();
  // --- Available data sources ---
    const sources = {
      calEvents: {
        fn: getCalendar,
        ttl: 300 // 5 min
      },
      stocks: {
        fn: getStockInfo,
        ttl: 300
      },
      tracking: {
        fn: getTrackingInfo,
        ttl: 300
      },
      layout: {
        fn: getLayoutData,
        ttl: 3600
      },
      makerworld: {
        fn: getMakerWorldStats,
        ttl: 600
      },
      weather: {
        fn: () => fetchOpenMeteo(45.5017, -73.5673, 4, 3),
        ttl: 900
      }
    };

    let requestedKeys;

    // --- No param → return ALL (but still optimized with cache) ---
    if (!e || !e.parameter || !e.parameter.data) {
      requestedKeys = Object.keys(sources);
    } else {
      requestedKeys = e.parameter.data
        .split(',')
        .map(k => k.trim())
        .filter(k => sources[k]);
    }

    const result = {};

    requestedKeys.forEach(key => {
      const cacheKey = "cache_" + key;

      // Try cache
      let cached = cache.get(cacheKey);

      if (cached) {
        result[key] = JSON.parse(cached);
        return;
      }

      // Fetch only if needed
      try {
        const data = sources[key].fn();
        result[key] = data;

        // Store in cache
        cache.put(cacheKey, JSON.stringify(data), sources[key].ttl);

      } catch (err) {
        result[key] = { error: err.toString() };
      }
    });

    // --- Add timestamp ---
    result.lastUpdated = new Date().toISOString();

    return ContentService
      .createTextOutput(JSON.stringify(result))
      .setMimeType(ContentService.MimeType.JSON);
  }
  const results = getAllInfo(); // fetch or get from cache
// Logger.log( results );
  // If no "data" param, return everything
  if (!e || !e.parameter || !e.parameter.data) {
    return ContentService
      .createTextOutput(JSON.stringify(results))
      .setMimeType(ContentService.MimeType.JSON);
  }

  // Support multiple keys separated by comma: ?data=stocks,weather
  const requested = e.parameter.data.split(',').map(k => k.trim());
  const filtered = {};

  requested.forEach(key => {
    if (results[key] !== undefined) {
      filtered[key] = results[key];
    }
  });

  return ContentService
    .createTextOutput(JSON.stringify(filtered))
    .setMimeType(ContentService.MimeType.JSON);
}

function debugCache() {
  const cache = CacheService.getScriptCache();
  const data = cache.get("storedData");

  Logger.log(data);
}


/**
 * Convert Open-Meteo weather code to a Material Design Icon (MDI) unicode
 */
function weatherCodeToMDI(code) {
  switch (code) {
    case 0:           return '☀'; // sunny
    case 1: case 2:   return '⛅'; // partly cloudy
    case 3:           return '☁'; // cloudy
    case 45: case 48: return '🌫'; // fog
    case 51: case 53: case 55: return '🌦'; // drizzle
    case 61: case 63: case 65: return '🌧'; // rain
    case 71: case 73: case 75: return '❄'; // snow
    case 80: case 81: case 82: return '🌧'; // rain showers
    case 85: case 86: return '❄';           // snow showers
    case 95: case 96: case 99: return '⛈';  // thunderstorm
    default: return '☁';
  }
}

/**
 * Fetch Open-Meteo daily forecast for given coordinates
 *
 * @param {number} latitude
 * @param {number} longitude
 * @param {number} days Number of days to fetch (1–7)
 * @param {number} maxRetries Optional, number of retries if request fails
 * @returns {Array} Array of day objects: {sunrise, sunset, temp_max, temp_min, weather_code, icon}
 */
function fetchOpenMeteo( ) {
  const cache = CacheService.getScriptCache();
  const cached = cache.get("weather_last_good");

  const latitude = Number(getExtra(8, 1));
  const longitude = Number(getExtra(8, 2));
  const days = Math.min(Math.max(Number(getExtra(8, 3)) || 1, 1), 7);

  if (!isFinite(latitude) || !isFinite(longitude)) {
    Logger.log("OpenMeteo skipped: invalid latitude/longitude in Layout row ID 8");
    return cached ? JSON.parse(cached) : null;
  }

  const url = `https://api.open-meteo.com/v1/forecast?latitude=${latitude}&longitude=${longitude}` +
              `&daily=weather_code,sunrise,sunset,temperature_2m_max,temperature_2m_min` +
              `&forecast_days=${days}&timezone=auto`;

  let payload = null;

  for (let attempt = 0; attempt < 3; attempt++) {
    try {
      const response = UrlFetchApp.fetch(url, { muteHttpExceptions: true });
      Logger.log(`OpenMeteo response code: ${response.getResponseCode()} attempt=${attempt + 1}`);
      if (response.getResponseCode() === 200) {
        payload = JSON.parse(response.getContentText());
        if (payload && payload.daily) break;
      }
    } catch (e) {
      Logger.log(`OpenMeteo attempt ${attempt + 1} failed: ${e}`);
    }

    Utilities.sleep(500 * (attempt + 1));
  }

  Logger.log(payload);
  if (!payload || !payload.daily) {
    return cached ? JSON.parse(cached) : null;
  }

  const daily = payload.daily;
  const result = [];

  for (let i = 0; i < days; i++) {
    result.push({
      sunrise: daily.sunrise[i] || '',
      sunset: daily.sunset[i] || '',
      temp_max: daily.temperature_2m_max[i] || 0,
      temp_min: daily.temperature_2m_min[i] || 0,
      weather_code: daily.weather_code[i] || 0,
      // icon: weatherCodeToMDI(daily.weather_code[i] || 0)
    });
  }

  cache.put("weather_last_good", JSON.stringify(result), 6 * 60 * 60);
  return result;
}


/**
 * Main function to get or add tracking info
 * @param {string} carrier - Courier name or id (use -1 for automatic)
 * @param {string} trackingNumber
 * @returns {Object} tracking info
 */
function getTrackingInfo() {
  refreshTracking();
  const ss = getSpreadsheet();
  const sheet = ss.getSheetByName("Tracking");
  if (!sheet) throw new Error("Sheet not found");

  const now = new Date();
  const data = sheet.getDataRange().getValues();
  const headers = data[0];

  const colCarrier = headers.indexOf("Carrier");
  const colCarrierID = headers.indexOf("CarrierID");
  const colTracking = headers.indexOf("TrackingNumber");
  const colLastStatus = headers.indexOf("LastStatus");
  const colDelivery = headers.indexOf("DeliveryStatus");
  const colLastChecked = headers.indexOf("LastChecked");
  const colStatus = headers.indexOf("Status");
  const colInserted = headers.indexOf("Inserted");

  const apiData = fetchAllShipments();

  if (!apiData || !apiData.payload) return [];
  const apiMap = {};
  apiData.payload.forEach(pkg => {
    apiMap[pkg.track_number] = pkg;
  });

  // Loop spreadsheet rows
  for (let i = 1; i < data.length; i++) {

    const row = i + 1;
    const trackingNumber = data[i][colTracking];
    const carrierID = data[i][colCarrierID];
    const inserted = data[i][colInserted];

    if (!trackingNumber) continue;

    // If Inserted empty → call update once
    if (!inserted) {
      insert(carrierID, trackingNumber);
      sheet.getRange(row, colInserted + 1).setValue(now);
      continue;
    }

    const pkg = apiMap[trackingNumber];
    if (!pkg) continue;

    const newStatus = pkg.last_status || "";
    const newDelivery = pkg.delivery_status || "";
    const newStatusInt = pkg.status || 0;

    if (data[i][colLastStatus] !== newStatus ||
        data[i][colDelivery] !== newDelivery) {

      sheet.getRange(row, colLastStatus + 1).setValue(newStatus);
      sheet.getRange(row, colDelivery + 1).setValue(newDelivery);
      sheet.getRange(row, colStatus + 1).setValue(newStatusInt);
      sheet.getRange(row, colLastChecked + 1).setValue(now);
    }
  }

  // Reload sheet (important after updates)
  const updatedData = sheet.getDataRange().getValues();

  // Build formatted result for ESP32
  const result = [];

  for (let i = 1; i < updatedData.length; i++) {

    const tracking = updatedData[i][colTracking];
    if (!tracking) continue;

    result.push({
      tracking:       String(updatedData[i][colTracking] || ""),
      status:         String(updatedData[i][colLastStatus] || ""),
      deliveryStatus: String(updatedData[i][colDelivery] || ""),
      lastChecked:    formatDate(updatedData[i][colLastChecked]),
      cached:         true
    });
  }

  return result;
}

function formatDate(d) {
  if (!d) return "";
  return Utilities.formatDate(
    new Date(d),
    Session.getScriptTimeZone(),
    "yyyy-MM-dd HH:mm:ss"
  );
}
/**
 * Fetch shipment info from PKGE
 * @param {string} trackingNumber
 * @returns {Object} lastStatus, deliveryStatus
 */
function updateTracking(trackingNumber) {
  const url = `${TRACK_URL}/update?trackNumber=${encodeURIComponent(trackingNumber)}`;
  Logger.log( url );
  // Logger.log( url );
  const options = {
    method: "post",
    headers: {
      "X-Api-Key": TRACK_API_KEY
    },
    muteHttpExceptions: true
  };
  Logger.log( options );

  try {
    const response = UrlFetchApp.fetch(url, options);
    Logger.log( response );
  } catch (e) {
    Logger.log("Error fetching shipment: " + e);
  }

  return null;
}


function insert(carrierID, trackNumber) {
  const url = `${TRACK_URL}?trackNumber=${encodeURIComponent(trackNumber)}&courierId=${carrierID}`;
  const options = {
    method: "post",
    headers: {
      "X-Api-Key": TRACK_API_KEY
    },
    muteHttpExceptions: true
  };
  
  try {
    const response = UrlFetchApp.fetch(url, options);
  } catch (e) {
    Logger.log("Error fetching shipment: " + e);
  }

  return null;
}


function fetchAllShipments() {

  const url = `${TRACK_URL}/list`;

  const options = {
    method: "get",
    headers: {
      "X-Api-Key": TRACK_API_KEY
    },
    muteHttpExceptions: true
  };

  try {
    const response = UrlFetchApp.fetch(url, options);

    if (response.getResponseCode() === 200) {
      return JSON.parse(response.getContentText());
    } else {
      Logger.log("Fetch ALL shipments error: " + response.getContentText());
    }

  } catch (e) {
    Logger.log("Error fetching shipments: " + e);
  }

  return null;
}

/**
 * Convert array row to object using headers
 */
function arrayToObject(row, headers) {
  const obj = {};
  headers.forEach((h, i) => obj[h] = row[i]);
  return obj;
}

/**
 * Convert inserted initial data array to object
 */
function initialDataToObject(dataArray, headers) {
  return arrayToObject(dataArray, headers);
}

function getExtra( id, extra) {
  const ss = getSpreadsheet();
  const sheet = ss.getSheetByName("Layout"); // same tab as before
  const data = sheet.getDataRange().getValues();

  const headers = data[0];

  const idIndex = headers.indexOf("ID");       // column where IDs are stored
  const extraIndex = headers.indexOf("Extra"+extra); // column containing secretKey

  if (idIndex === -1 || extraIndex === -1  ) {
    // Logger.log("ID or extra1 column not found");
    return null;
  }

  // Filter rows where ID 
  const row = data.find(r => r[idIndex] === id);

  if (!row) {
    // Logger.log("No row with ID");
    return null;
  }

  const value = row[extraIndex];
  // Logger.log("Value from ID : " + value);
  return value;
}

function refreshTracking() {
  const ss = getSpreadsheet();
  const sheet = ss.getSheetByName("Tracking");
  if (!sheet) {
    Logger.log("Tracking sheet not found, skipping refresh.");
    return;
  }

  const data = sheet.getDataRange().getValues();
  if (data.length < 2) {
    Logger.log("No tracking data found, skipping refresh.");
    return;
  }

  const headers = data[0];

  const colTracking    = headers.indexOf("TrackingNumber");
  const colInserted    = headers.indexOf("Inserted");
  const colLastChecked = headers.indexOf("LastChecked");

  if (colTracking === -1 || colLastChecked === -1) {
    Logger.log("Required columns not found.");
    return;
  }

  const now = new Date();
  const FOUR_HOURS = 4 * 60 * 60 * 1000;

  for (let i = 1; i < data.length; i++) {
    const trackingNumber = data[i][colTracking];
    const inserted       = data[i][colInserted];
    const lastChecked    = data[i][colLastChecked];

    if (!trackingNumber) continue;  // no tracking number
    if (!inserted) continue;        // not inserted yet

    let shouldUpdate = false;

    // If never checked before → update
    if (!lastChecked) {
      shouldUpdate = true;
    } else {
      const last = new Date(lastChecked);
      const diff = now - last;

      if (diff >= FOUR_HOURS) {
        shouldUpdate = true;
      }
    }

    if (shouldUpdate) {
      // Logger.log("Updating: " + trackingNumber);

      updateTracking(trackingNumber);

      // Update LastChecked timestamp
      sheet.getRange(i + 1, colLastChecked + 1).setValue(now);
    }
  }

  Logger.log("Tracking refresh completed at " + now);
}
