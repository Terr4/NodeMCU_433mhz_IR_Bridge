#include <ESP8266WiFi.h>
#include <ESPiLight.h>
#include <IRremoteESP8266.h>

/*
 * D5 = GPIO 14 (grey) (433 receiver) (not used now)
 * D6 = GPIO 12 (purple) (IR diode)
 * D7 = GPIO 13 (green) (433 transmitter)
 * D8 = GPIO 15
 */

//433Mhz Config
const int TRANSMITTER_PIN = 13;
String deviceid     = "";
String state        = "";
String json         = "";
int systemcode      = 0;
int unitcode        = 0;
int response        = -1;
ESPiLight rf(TRANSMITTER_PIN);

//IR Config
const int LED          = 15;
const int irbreak      = 200;
String restrequest     = "";
boolean gotresponse    = false;
int charindex          = 0;
char character;
IRsend irsend(12); //send diode on pin 12
decode_results results;

//WiFi Config
const char* ssid     = "SSID_NAME";
const char* password = "PASS";
WiFiServer server(5001); //webserver on port 5001


void setup() {
  Serial.begin(115200);
  delay(10);

  // Connect to WiFi network
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA); //else it opens an own SSID if this is not set
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");

  // Start the server
  server.begin();
  Serial.println("Server started");

  // Print the IP address
  Serial.print("Use this URL to connect: ");
  Serial.print("http://");
  Serial.print(WiFi.localIP());
  Serial.println("/");

  //Start the ir receiver
  irsend.begin();

}
 
void loop() {

  // Check if a client has connected
  WiFiClient client = server.available();
  if (!client) {
    return;
  }
 
  // Wait until the client sends some data
  Serial.println("new client");
  while(!client.available()){
    delay(1);
  }
 
  // Read the first line of the request
  String request = client.readStringUntil('\r');

  // trim to the raw message 
  if( request.indexOf(" HTTP") > 0 ) {
    request = request.substring( 0 , request.indexOf(" HTTP") );
  }
  
  Serial.println(request);
  client.flush();

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println(""); //  do not forget this one
  client.println("<!DOCTYPE HTML>");
  client.println("<html>");
  client.println("<br/><br/>");

  /*
  check if we have received a 433Mhz PiLight Command
  example requests
  http://192.168.0.115/control?device=Outlet01&state=off
  http://192.168.0.115/control?device=Outlet01&state=on
  */
  if (request.indexOf("/control?device=") != -1)  {

    //if we find the ampersant, the state and device the request "looks" ok
    if(request.indexOf("&") != -1 && request.indexOf("state") != -1 && request.indexOf("device") != -1) {

      deviceid = request.substring( request.indexOf("device=")+7 , request.indexOf("&") );
      state    = request.substring( request.indexOf("state=")+6 , request.indexOf(" HTTP") );

      //if it is a valid state we can switch
      if(state == "on" or state == "off") {
  
        if( deviceid.indexOf("Outlet") == 0 ) {
          unitcode = deviceid.substring(6).toInt();
        }

        if( unitcode > 0 && unitcode < 99 ) {

          //hardcoded brennenstuhl plug set
          systemcode = 31;

          json = "{\"systemcode\":" + String(systemcode) + ",\"unitcode\":" + String(unitcode) + ",\"" + state + "\":1}";
          response = rf.send( "elro_800_switch" , json);

          // Return the response
          client.println( F("Received Valid 433 Request<br/>") );
          client.println( "DeviceID: " + String(deviceid) + "<br/>");
          client.println( "State: " + state + "<br/>" );
          client.println( "Unitcode: " + String(unitcode) + "<br/>" );
          client.println( "JSON: " + json + "<br/>");
          if( response == 50 ) {
            client.println( "Response: <b>OK (" + String(response) + ")</b><br/>");
          } else {
            client.println( "Response: <b>FAIL (" + String(response) + ")</b><br/>");
          }

        } else {
          client.println( F("<span style=\"color:red\">Invalid unitcode received. use only positive numbers < 99</span><br/>") );
        }

      } else {
        client.println( F("<span style=\"color:red\">Invalid state received. use only 'on' or 'off'</span><br/>") );
      }

    } else {
      client.println( F("<span style=\"color:red\">Invalid state received. <br/>Example: control?device=Outlet02&state=on</span><br/>") );
    }




  // check if we have received an IR REST Command
  /*
  example requests
  http://192.168.0.115/rest/ircmd/4CB3748B
  http://192.168.0.115/rest/ircmd/2x4CB3748B
  http://192.168.0.115/rest/irmacro/projectoroff
  */
  } else if( request.indexOf("/rest/ir") != -1 ) {

    restrequest = "";
    gotresponse = false;
    charindex   = 0;

    Serial.println(F("incoming IR request"));
    
    //if we find the ampersant, the state and device the request "looks" ok
    if(request.indexOf("ircmd") != -1 or request.indexOf("irmacro") != -1) {
      if(request.indexOf("ircmd") != -1) {
        restrequest = request.substring( request.indexOf("ircmd/") );
      } else if(request.indexOf("irmacro") != -1) {
        restrequest = request.substring( request.indexOf("irmacro/") );
      }
      client.println( F("Received Valid IR REST Request<br/>") );
      client.println( "IR REST Request: " + String(restrequest) + "<br/>");
      parseRestRequest(restrequest, client);
    } else {
      client.println( F("<span style=\"color:red\">Invalid irtype received. Use 'ircmd' or 'irmacro'<br/>") );
    }

  } else {
    client.println( F("<span style=\"color:red\">Unknown Request received. <br/>Example: control?device=Outlet02&state=on</span><br/>") );
  }

  client.println("</html>");
   
  delay(1);
  Serial.println("Client disconnected");
  Serial.println("");
 
}




//-----------------------------------------IR Sending------------------------------------

void parseRestRequest(String restrequest, WiFiClient client) {
  Serial.print(restrequest);
  Serial.print(F(" index of x: "));
  Serial.println(restrequest.indexOf('x'));
  if(restrequest.startsWith(F("irmacro"))) {
    sendIRMacro(restrequest.substring(8));
    client.println( "Response: <b>OK (IR Macro)</b><br/>");
  } else if(restrequest.startsWith(F("ircmd"))) {
    if( restrequest.indexOf('x') > 0 ) {
      client.println( "Response: <b>OK (multiple IR command)</b><br/>");
      // ircmd/3xAAAAAAA
      sendIRCmd( strtol(restrequest.substring(8,16).c_str(),NULL,16) , restrequest.substring(6,7).toInt() );
    } else {
      client.println( "Response: <b>OK (single IR command)</b><br/>");
      // ircmd/AAAAAAA
      Serial.println(F("no repeat defined, set 1"));
      sendIRCmd( strtol(restrequest.substring(6,14).c_str(),NULL,16) , 1 );
    }

  }
}

/* some IR codes
TURN PROJECTOR ON:        4CB340BF Type: 3 Length: 32 Type: NEC
TURN PROJECTOR OFF:       4CB3748B Type: 3 Length: 32 Type: NEC
Projector Resync & right  4CB348B7 Type: 3 Length: 32 Type: NEC
Projector 3D SBS On       4CB32AD5 + 4CB328D7 + 4CB3F00F (menu 3d + down + enter
Projector 3D SBS Off      4CB32AD5 + 4CB38877 + 4CB3F00F (menu 3d + up + enter)
Projector VGA             4CB3D827
Projector HDMI1           4CB36897
Projector HDMI2           4CB30CF3
Projector Video           4CB338C7
Projector Component       4CB3E817
TURN AV RECEIVER ON/OFF:  7E8154AB Type: 3 Length: 32 Type: NEC
Projector Resync          macro resyncHDMI1 -> 4CB30CF3 + 4CB36897
*/

void sendIRCmd(long code, int repeat) {
  Serial.print(F("EXEC IR CMD: "));
  Serial.print(String(code));
  Serial.print(F(" and repeat "));
  Serial.print(String(repeat));
  Serial.println(F(" times"));
  wildblinking(1);
  for(int z=0; z<repeat; z++) {
    //hardcoded NEC with length 32, all my devices work with that
    Serial.print(F("send+++++> "));
    Serial.println(String(code));
    irsend.sendNEC(code, 32);
    delay(irbreak);
  }

}

void sendIRMacro(String macroname) {

  Serial.print(F("EXEC MACRO: "));
  Serial.println(macroname);
  wildblinking(5);

  boolean macrofound = true;

  if(macroname.equals(F("projectoroff"))) {
    sendIRCmd(1286829195, 1);

  } else if(macroname.equals(F("projectoron"))) {
    sendIRCmd(0x4CB340BF, 1);

  } else if(macroname.equals(F("resyncHDMI1"))) {
    sendIRCmd(0x4CB30CF3, 1);
    delay(1500);
    sendIRCmd(0x4CB36897, 1);

  } else if(macroname.equals(F("projector3don"))) {
    sendIRCmd(0x4CB32AD5, 1);
    delay(1000);
    sendIRCmd(0x4CB328D7, 1);
    delay(1000);
    sendIRCmd(0x4CB3F00F, 1);

  } else if(macroname.equals(F("projector3doff"))) {
    sendIRCmd(0x4CB32AD5, 1);
    delay(1000);
    sendIRCmd(0x4CB38877, 1);
    delay(1000);
    sendIRCmd(0x4CB3F00F, 1);

  } else if(macroname.equals(F("roombapower"))) {
    roomba_sendircmd("power");
  } else if(macroname.equals(F("roombadock"))) {
    roomba_sendircmd("dock");
  } else if(macroname.equals(F("roombapause"))) {
    roomba_sendircmd("pause");
  } else if(macroname.equals(F("roombaclean"))) {
    roomba_sendircmd("clean");
  } else {
    boolean macrofound = false;
  }
/*
  if(macrofound) {
    //reenable IR receiver after sending
    irrecv.enableIRIn();
  }
*/
}

//-----------------------------------------Roomba Control------------------------------------
/* 
 Roomba IR codes
 sources:
 http://lirc.sourceforge.net/remotes/irobot/Roomba
 https://github.com/bynds/makevoicedemo/blob/master/Arduino/roomba_ir/roomba_ir.ino
 http://makezine.com/projects/use-raspberry-pi-for-voice-control/
 http://www.enide.net/webcms/?page=tiny-remote
 http://astrobeano.blogspot.co.at/2013/10/roomba-620-infrared-signals.html
 https://gist.github.com/peterjc/7127551
*/
void roomba_sendircmd(String cmd)
{

  unsigned int clean[15]  = {3000,1000,1000,3000,1000,3000, 1000,3000,3000,1000, 1000,3000, 1000,3000,1000};
  unsigned int power[15]  = {3000,1000,1000,3000,1000,3000, 1000,3000,3000,1000, 1000,3000, 3000,1000,1000};  
  unsigned int dock[15]   = {3000,1000,1000,3000,1000,3000, 1000,3000,3000,1000, 3000,1000, 3000,1000,3000};
  unsigned int pause[15]  = {3000,1000,1000,3000,1000,3000, 1000,3000,3000,1000, 1000,3000, 1000,3000,3000};
  unsigned int ircmd[15];

  //repeat ir commands 3 times
  for (int i = 0; i < 3; i++) {
    if(cmd.equals("clean")) {
      irsend.sendRaw(clean, 15, 38);
    } else if(cmd.equals("power")) {
      irsend.sendRaw(power, 15, 38);
    } else if(cmd.equals("dock")) {
      irsend.sendRaw(dock, 15, 38);
    } else if(cmd.equals("pause")) {
      irsend.sendRaw(dock, 15, 38);
    }
    delay(50);
  }

}

void wildblinking(int qty) {

}
