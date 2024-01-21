
/*

-------------------------------
************ STATS ************

  Moisture sensor
    --------------------------------------------
    | 303    -   water   -  100%               |  
    | 727    -   dry     -  0%                 |
    | Result ->  (1 - (pomiar - 303)/424)*100% |
    --------------------------------------------
  Water flow
    ------------------
    | 5s    - 150 ml | 
    | 1s    - 30ml   |
    ------------------
   
   Github repository to SimplePgSQL https://github.com/ethanak/SimplePgSQL?tab=readme-ov-file
  
*/

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <SimplePgSQL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include <PID_v1.h> // Brett Beauregard PID



ESP8266WebServer server(80);

// Network credentials and data
IPAddress PGIP(192,168,0,12);        // your PostgreSQL server IP

const char* ssid = "CGA2121_LAEeVPc";
const char* pass = "Y3WxcydCRSBGdEQkGS";

const char user[] = "postgres";       // your database user
const char password[] = "1234567890";   // your database password
const char dbname[] = "postgres";         // your database name

int WiFiStatus;
WiFiClient client;

char buffer[1024];
PGconnection conn(&client, 0, 1024, buffer);



// PID setup
double setpoint = 50.0;  // Poziom wilgotności, który chcemy utrzymać
double Kp = 2.0;  // Wzmocnienie proporcjonalne
double Ki = 0.1;  // Wzmocnienie całkujące
double Kd = 1.0;  // Wzmocnienie różniczkujące
double input, output;

PID pid(&input, &output, &setpoint, Kp, Ki, Kd, DIRECT);



// ESP Setup
void setup(void)
{
  Serial.print("Setup start ... ");

  // Pins setup 
  pinMode(A0, INPUT);    // Odczyt wilgotności 
  pinMode(D7, OUTPUT);   // Pompka
  digitalWrite(2, HIGH);
 
  Serial.begin(115200);

  // PID setup - manual is default
  pid.SetMode(AUTOMATIC);

  // WiFi setup
  delay(10);
  Serial.print('\n');

  WiFi.begin(ssid, pass);
  Serial.print("Connecting to ");
  Serial.print(ssid); Serial.println(' ... ');

  int i = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(++i); Serial.print(' ');
  }

  Serial.println('\n');
  Serial.println("WiFi connection established!");  
  Serial.print("IP address:\t");
  Serial.println(WiFi.localIP()); 


  // Request 
  server.on("/receive_message", HTTP_POST, []() {
    String jsonString = server.arg("plain");

    // Deserializacja JSON
    DynamicJsonDocument jsonDocument(256);  // Dostosuj rozmiar dokumentu do potrzeb
    DeserializationError error = deserializeJson(jsonDocument, jsonString);

    if (error) {
      Serial.print("Błąd deserializacji JSON: ");
      Serial.println(error.c_str());
      server.send(400, "text/plain", "Błąd deserializacji JSON");

      // Try co connect again
      PGconnection conn(&client, 0, 1024, buffer);
      return;
    }

    // Pobranie wartości pól z JSON
    String action = jsonDocument["action"].as<String>();
    int status = jsonDocument["status"];
    int timeValue = jsonDocument["time"];

    Serial.println("Odebrano request: action: " + action + " time: " + String(timeValue));

    // Wykonanie odpowiednich działań w zależności od zawartości JSON
    if (action == "water" && status == 1) {
      digitalWrite(D7, HIGH);
      delay(timeValue);
      digitalWrite(D7, LOW);
      server.send(200, "text/plain", "Akcja 'water' została wykonana");
    } else {
      server.send(200, "text/plain", "Otrzymano JSON, ale nie wykonano żadnej akcji");
    }
  });

  server.begin();
    
}

// To check connection during main loop 
void checkConnection()
{
    Serial.println("Checking connection");
    int status = WiFi.status();
    if (status != WL_CONNECTED) {
        if (WiFiStatus == WL_CONNECTED) {
            Serial.println("Connection lost");
            WiFiStatus = status;
        }
    }
    else {
        if (WiFiStatus != WL_CONNECTED) {
            Serial.println("Connected");
            WiFiStatus = status;
        }
    }
}


static PROGMEM const char query_rel[] = "\
SELECT a.attname \"Column\",\
  pg_catalog.format_type(a.atttypid, a.atttypmod) \"Type\",\
  case when a.attnotnull then 'not null ' else 'null' end as \"null\",\
  (SELECT substring(pg_catalog.pg_get_expr(d.adbin, d.adrelid) for 128)\
   FROM pg_catalog.pg_attrdef d\
   WHERE d.adrelid = a.attrelid AND d.adnum = a.attnum AND a.atthasdef) \"Extras\"\
 FROM pg_catalog.pg_attribute a, pg_catalog.pg_class c\
 WHERE a.attrelid = c.oid AND c.relkind = 'r' AND\
 c.relname = %s AND\
 pg_catalog.pg_table_is_visible(c.oid)\
 AND a.attnum > 0 AND NOT a.attisdropped\
    ORDER BY a.attnum";

static PROGMEM const char query_tables[] = "\
SELECT n.nspname as \"Schema\",\
  c.relname as \"Name\",\
  CASE c.relkind WHEN 'r' THEN 'table' WHEN 'v' THEN 'view' WHEN 'm' THEN 'materialized view' WHEN 'i' THEN 'index' WHEN 'S' THEN 'sequence' WHEN 's' THEN 'special' WHEN 'f' THEN 'foreign table' END as \"Type\",\
  pg_catalog.pg_get_userbyid(c.relowner) as \"Owner\"\
 FROM pg_catalog.pg_class c\
     LEFT JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace\
 WHERE c.relkind IN ('r','v','m','S','f','')\
      AND n.nspname <> 'pg_catalog'\
      AND n.nspname <> 'information_schema'\
      AND n.nspname !~ '^pg_toast'\
  AND pg_catalog.pg_table_is_visible(c.oid)\
 ORDER BY 1,2";

int pg_status = 0;

// This function communicates with postgresql database 
void doPgSqlQuery(char inbuf[128])
{
    char *msg;
    int rc;

    // Check connection with database 
    if (!pg_status) {
        conn.setDbLogin(PGIP,
            user,
            password,
            dbname,
            "utf8");
        pg_status = 1;
        return;
    }

    Serial.println(pg_status);

    // Logged 
    if (pg_status == 1) {
        rc = conn.status();
        Serial.println(rc);
        // There is something wrong with connection
        if (rc == CONNECTION_BAD || rc == CONNECTION_NEEDED) {
            char *c=conn.getMessage();
            if (c) {
              Serial.println("Connection error: ");
              Serial.println(c);
              Serial.println("End of eonnection error");
            }
            pg_status = -1;
        }
        // Connection is fine, we can go further 
        else if (rc == CONNECTION_OK) {
            pg_status = 2;
            Serial.println("Enter query");
        }
        return;
    }
    
    // Part where we sent data to postgresql
    if (pg_status == 2) {
        int n = 0;
        while(inbuf[n]) {
          ++n;
        }       
        Serial.print(inbuf);
        while (n > 0) {
            if (isspace(inbuf[n-1])) n--;
            else break;
        }
        inbuf[n] = 0;

        if (!strcmp(inbuf,"\\d")) {
            if (conn.execute(query_tables, true)) goto error;
            Serial.println("Working...");
            pg_status = 3;
            return;
        }
        if (!strncmp(inbuf,"\\d",2) && isspace(inbuf[2])) {
            char *c=inbuf+3;
            while (*c && isspace(*c)) c++;
            if (!*c) {
                if (conn.execute(query_tables, true)) goto error;
                Serial.println("Working...");
                pg_status = 3;
                return;
            }
            if (conn.executeFormat(true, query_rel, c)) goto error;
            Serial.println("Working...");
            pg_status = 3;
            return;
        }

        if (!strncmp(inbuf,"exit",4)) {
            conn.close();
            Serial.println("Thank you");
            pg_status = -1;
            return;
        }
        if (conn.execute(inbuf)) goto error;
        Serial.println("Working...");
        pg_status = 3;
    } 
    if (pg_status == 3) {
        rc=conn.getData();
        int i;
        if (rc < 0) goto error;
        if (!rc) return;
        if (rc & PG_RSTAT_HAVE_COLUMNS) {
            for (i=0; i < conn.nfields(); i++) {
                if (i) Serial.print(" | ");
                Serial.print(conn.getColumn(i));
            }
            Serial.println("\n==========");
        }
        else if (rc & PG_RSTAT_HAVE_ROW) {
            for (i=0; i < conn.nfields(); i++) {
                if (i) Serial.print(" | ");
                msg = conn.getValue(i);
                if (!msg) msg=(char *)"NULL";
                Serial.print(msg);
            }
            Serial.println();
        }
        else if (rc & PG_RSTAT_HAVE_SUMMARY) {
            Serial.print("Rows affected: ");
            Serial.println(conn.ntuples());
        }
        else if (rc & PG_RSTAT_HAVE_MESSAGE) {
            msg = conn.getMessage();
            if (msg) Serial.println(msg);
        }
        if (rc & PG_RSTAT_READY) {
            pg_status = 2;
            Serial.println("Enter query");
        }
    }
    return;
error:
    msg = conn.getMessage();
    if (msg) Serial.println(msg);
    else Serial.println("UNKNOWN ERROR");
    if (conn.status() == CONNECTION_BAD) {
        Serial.println("Connection is bad");
        pg_status = -1;
    }
}

// SQL Query function
char* sqlQueryMoisture(const char moisture[3],const char timeCnt[10]) {
  char* result = (char*)malloc(128 * sizeof(char));
  snprintf(result, 128, "INSERT INTO data (time, sensor1) VALUES (%s, %s);", timeCnt, moisture);
  return result;
}

float moisture = 0.0;
int timeCnt = 0;

// 1 - watering | 0 - wait
int systemStatus = 1;

void loop() {

  int moistureRead = analogRead(A0);

  // 
  if(moistureRead < 295) {
    moistureRead = 303;
  } else if(moistureRead > 700) {
    moistureRead = 727;
  }

  double moisture = (1 - (moistureRead - 290) / 440.0)*100;
  Serial.println("Moisture2: " + String((int)moisture) + "% Read analog value: " + String(analogRead(A0))); 

  input = moisture;

  // Regulacja PID
  pid.Compute();

  // Włączanie/wyłączanie pompki na podstawie sterowania PID
  if (output > 0) {
    systemStatus = 1;
    digitalWrite(D7, HIGH);  // Włącz pompkę
    delay(1000);
    digitalWrite(D7, LOW);   // Wyłącz pompkę
  } else {
    systemStatus = 0;
    digitalWrite(D7, LOW);   // Wyłącz pompkę
    delay(5000);
  }

  // PID log
  Serial.print("Input: ");
  Serial.print(input);
  Serial.print(" | Output: ");
  Serial.println(output);


  checkConnection();
  if (WiFiStatus == WL_CONNECTED) { 

    server.handleClient();

    // Send data to postgre 
    char moistureChar[3];
    char timeChar[10];

    sprintf(moistureChar, "%d", (int)moisture);
    sprintf(timeChar, "%d", (int)timeCnt);

    // Query only if system is in wait state
    if(systemStatus == 0) {
      timeCnt++;
      char* querry = sqlQueryMoisture(moistureChar, timeChar);
      doPgSqlQuery(querry);
    }
  }
  
  delay(2000);

}
