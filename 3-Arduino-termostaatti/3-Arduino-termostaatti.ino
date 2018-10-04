//**********************************************************************************************
// #3 Arduino termostaatti
// Led näyttöinen termostaatti asetettavalla lämpötilalla ja hystereesillä.
// DS18B20, Arduino Uno, VMA209 monitoimi shield, POLOLU-2480
//**********************************************************************************************/

// Tarvittavat kirjastot
#include <OneWire.h>
#include <DallasTemperature.h>
#include <EEPROM.h>

//Annetaan selkeät nimet käytetyille Arduinon pinneille
// Led näytön ohjaus 74HC595 piireillä VMA209 kortilla
#define HC595LATCHPIN 4
#define HC595CLOCKPIN 7
#define HC595DATAPIN 8
// DS18B20 anturin One Wire väylä
#define ONE_WIRE_BUS A4
// Releen ohjauspinni
#define RELAY 5
//VMA209 kortin 3 ohjaus painonappia
#define S1 A1 //S1, + painonappi
#define S2 A2 //S2, - painonappi
#define S3 A3 //S3, Enter painonappi

//Määritetään muutama vakio
//DS18B20 lukuviive loopin kierroksina, yksi kierros on noin 6ms. Anturin lukeminen aiheuttaa pienen välähdyksen näytössä, joten mitä harvemmin anturia luetaan sitä tasaisempi näyttö on.
//6*500ms = 3sec  Kokonaisviive ei saa olla alle 1 sec (Lämpötilan mittaus anturilla kestää noin 1 sec)
#define DS18B20_DELAY 500 //noin 3 sec
//Viive ennen kuin +/- näppäimen pitkän painalluksen pika kelaus aktivoidaan
#define SPEED_INCREACE_DELAY 100 // noin 0,6 sec

// DS18B20 anturin käyttämän One Wire väylän alustus
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
DeviceAddress tempDeviceAddress;

//Led näytön segmenttien ohjaus.
//74HC595 lähetettävä data kutakin näytettävää numeroa/merkkiä kohden
// 0 kyseinen segmentti hohtaa, 1 kyseinen segmentti on pimeä
//  indeksi listassa:  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 , 11, 12, 13, 14, 15
//  näytettävä merkki: 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, " ",  -,  n,  F,  r,  E
unsigned char Segmentit[] = {B11000000,B11111001,B10100100,B10110000,B10011001,B10010010,B10000010,B11111000,B10000000,B10010000,B11111111,B10111111,B10101011,B10001110,B10101111,B10000110};
//74HC595 lähetettävä data, jotta voidaan valita mitä yksittäistä numeroa ohjataan kulloinkin.
unsigned char Numeronvalinta[]   = {B00000001,B00000010,B00000100,B00001000};
//Näytön numeroiden puskuri, mitä numeroa aiotaan missäkin näyttää
unsigned char bufferi[]  =  {11, 11, 11, 11}; //alustus arvo: - - - -

//Muuttuja jolla määritetään missä toimintatilassa/näyttötilassa laite kulloinkin on riippuen näppäimien painalluksesta
unsigned char menu_state = 0;

//Muuttuja anturilta luettua lämpötilaa varten, celsius asteina.
float temperature = 0.0;
//Anturin lämpötila muutettuna kokonaisluvuksi, kokonaisluvun viimeinen numero on lämpötilan ensimmäinen desimaali. esim. 13.65C -> 136
int temp_int = 0;
//Muuttujat DS18B20_DELAY ja SPEED_INCREACE_DELAY viiveiden laskemisen toteuttamiseksi
int measurement_timer = 0;
char speed_increace_timer = 0;

//Muuttujat termostaatin kytkentä ja poiskytkentä lämpötiloille
int on_temp = 200;
int off_temp = 250;

//Ohjelman alku asetukset
void setup ()
{
  //Asetetaan pinnien suunnat oikeiksi
  //Näytön ohjaus
  pinMode(HC595LATCHPIN,OUTPUT);
  pinMode(HC595CLOCKPIN,OUTPUT);
  pinMode(HC595DATAPIN,OUTPUT);
  //rele
  pinMode(RELAY,OUTPUT);
  //näppäimet
  pinMode(S1,INPUT); //+
  pinMode(S2,INPUT); //-
  pinMode(S3,INPUT); //enter

  //Asetetaan sarjaportin parametrit ja lähetetään alkuviesti
  Serial.begin(9600);
  Serial.println("Arduino termostaatti");
  //Luetaan laitteen sisäisestä EEPROM muistista aiemmin tallennetut on/off lämpötila-arvot
  
  EEPROM.get(0, temp_int);
  //Jos Arduinon EEPROM muisti on tyhjä, käytetään oletusarvoja muistin arvojen sijaan.
  if (temp_int<-550){
    on_temp = 200;
  }else{
    on_temp = temp_int;
  }
  EEPROM.get(32, temp_int);
  if (temp_int<-550){
    off_temp = 250;
  }else{
    off_temp = temp_int;
  }
  
  //Aloitetaan DS18B20 väylän toiminta
  sensors.begin();
  //Selvitetään anturin osoite
  sensors.getAddress(tempDeviceAddress, 0);
  //Kerrotaan kirjastolle ettei se jää odottamaan anturin lämpötilan muunnoksen valmistumista. (Aiheuttaisi 1 sec pysähdyksen aina kun anturia luetaan)
  sensors.setWaitForConversion(false);
  //
  sensors.requestTemperatures();
  //Päivitetään näyttö näyttämään oletus - - - -
  display();
}

//Aliohjelma näytön päivittämiseksi.
//Näytön toimintatavasta johtuen, jokaista näytön numeroa väläytetään vuorotelleen niin nopeasti ettei silmä havaitse vilkkumista.
//Aliohjelma skannaa näytön numerot kerran läpi ja näyttää jokaisen numeron kohdalla oikean merkin.
//Aliohjelmaa on kutsuttava toistuvasti ja riittävän usein, jotta näytön lukema näkyy kokonaisuudessaan ilman vilkkumista.
void display()
{
  //Näytössä yhteensä 4 numeroa, käydään kaikki läpi [i] kuvaa yksittäisen numeron indeksiä 0(vasen)-3(oikea)
  for(char i=0; i<=3; i++)
  {
    //Asetetaan 74HC595 LACH pinni alas jotta siirtorekisteriin voidaan kirjoittaa
    digitalWrite(HC595LATCHPIN,LOW);
    //Mikäli seuraavaksi päivitettävä numero on [2] näytetään desimaali piste
    if ((i==2) && (bufferi[2]<10)){
      shiftOut(HC595DATAPIN,HC595CLOCKPIN,MSBFIRST,Segmentit[bufferi[i]]-B10000000); //Lähetetään siirtorekisterille näytettävää numeroa vastaava data ja desimaalipiste
    }else{
      shiftOut(HC595DATAPIN,HC595CLOCKPIN,MSBFIRST,Segmentit[bufferi[i]]); //Lähetetään siirtorekisterille näytettävää numeroa vastaava data
    }
    shiftOut(HC595DATAPIN,HC595CLOCKPIN,MSBFIRST,Numeronvalinta[i] );  //Lähetetään toiselle siirtorekisterille tieto mitä numeroa 1/4 kuuluu näyttää
    //Asetetaan 74HC595 LACH pinni ylos jotta siirtorekisteriin lähetetty data siirtyy aktiiviseksi lähtöön
    digitalWrite(HC595LATCHPIN,HIGH);
    //2ms tauko
    delay(2);
  }

}

//Varsinaise ohjelma
//ohjelma toimii 5 eri tilassa riippuen menu_state muuttujan arvosta
//menu_state: 0 normaali toiminta, mitataan ja näytetään nykyinen lämpötila, rele aktivoituu lämpötilan mukaan
//            1 Menu ON, näytössä "On" painamalla enteriä voidaan muuttaa on_temp arvoa siirtymällä menu_state=4
//            2 Menu OFF, näytössä "OFF" painamalla enteriä voidaan muuttaa off_temp arvoa siirtymällä menu_state=5 
//            3 Menu RET, näytössä "On" painamalla enteriä palataan menu_state=0 ja jatketaan normaalia toimintaa ja tallennetaan muutetut on_temp ja off_temp arvot EEPROM muistiin
//            4 Näytetään nykyinen on_temp arvo, arvoa voi muuttaa +/- näppäimillä, enter painikeella palataan menu_state=1
//            5 Näytetään nykyinen off_temp arvo, arvoa voi muuttaa +/- näppäimillä, enter painikeella palataan menu_state=2
void loop()
{
  //********************************************************************************
  // Normaali toiminta, menu_state = 0
  //********************************************************************************
  if (menu_state == 0){
    //Jos jotain painiketta painetaa siirrytään "valikkoon" menu_state=1
    if (digitalRead(S1)== LOW || digitalRead(S2)== LOW || digitalRead(S3)== LOW){
      menu_state = 1;
      //Varmistetaan että rele sammuu kun vaihdetaan toimintatilaa
      digitalWrite(RELAY,LOW);
      //Odotellaan että käyttäjä vapauttaa painikeen, mutta päivitetään näyttö kuitenkin sillä välin (muuten näyttö olisi pimeä kun painonappi on pohjassa)
      while (digitalRead(S1)== LOW || digitalRead(S2)== LOW || digitalRead(S3)== LOW){
        display();
      }
    }

    //Koska anturin lämpötilan luku on hidasta odotellaan että se tulee valmiiksi, päivitetään näyttöä sillä välin
    if (measurement_timer < DS18B20_DELAY){
      //Aikaviive ei ole vielä kulunut, kasvatetaan muuttujaa
      measurement_timer++;
    }else{
      //Määritetty aikaviive on kulunut
      //nollataan aikaviive muuttuja
      measurement_timer = 0;
      //Luetaan anturin muistista sen mittaama lämpötila
      //ja lähetetään sen myös sarjaportilla
      Serial.print("Mitattu lämpötila: ");
      temperature = sensors.getTempC(tempDeviceAddress);
      Serial.println(temperature);
      //muunnetaan lämpötila helpommin käsiteltävään kokonaisluku muotoon
      temperature = temperature*10;
      temp_int = (int) temperature;
      //Tarkistetaan onko lukema sallituissa rajoissa, mikäli anturin yhteys ei toimi, kirjasto ilmoittaa lämpötilaksi -127C (temp_int on tällöin -1270)
       if (temp_int > -1000){
        // jos anturi toimii oikein verrataan on_temp ja off_temp lämpötiloja ja aktivoidaan rele tarvittaessa
        //Jos off_temp on määritetty alhaisemmaksi kuin on_temp termostaatti toimii "jäähdytys" tilassa
        if (off_temp<on_temp){
          //Jos mitattu lämpötila on alhaisempi kuin off_temp sammutetaan rele (liian kylmä, lopetetaan jäähdytys)
          if (temp_int<off_temp){
            digitalWrite(RELAY,LOW);
          }
          //jos mitattu lämoötila on korkeampi kuin on_temp aktivoidaan rele (liian kuuma, aktivoidaan jäähdytys)
          //muussa tapauksessa releen tilaa ei muuteta
          if (temp_int>on_temp){
            digitalWrite(RELAY,HIGH);
          }
        }else{ //Jos on_temp on määritetty alhaisemmaksi kuin off_temp termostaatti toimii "lämmitys" tilassa
          //Jos mitattu lämpötila on alhaisempi kuin on_temp aktivoidaan rele (liian kylmä, aktivoidaan lämmitys)
          if (temp_int<on_temp){
            digitalWrite(RELAY,HIGH);
          }
          //jos mitattu lämoötila on korkeampi kuin off_temp sammutetaan rele (liian kuuma, lopetetaan lämmitys)
          //muussa tapauksessa releen tilaa ei muuteta
          if (temp_int>off_temp){
            digitalWrite(RELAY,LOW);
          }
        }
      }else{
        // jos anturin yhteydessä on ongelma, sammutetaan rele
        digitalWrite(RELAY,LOW);
      }
      //Kirjoitetaan bufferiin mitattua lämpötilaa vastaavat numerot niiden näyttämistä varten
      if (temp_int <0){ // negatiiviset lämpötilat
        if (temp_int < -1000){ // jos anturin yhteysvirhe näytetään: no S (no sensor)
          bufferi[0] = 12; //n
          bufferi[1] = 0;  //O
          bufferi[2] = 10; //" "
          bufferi[3] = 5;  //S
        }else{ //anturi toimii näytetään lämpötila
          bufferi[0]= 11; //- merkki alkuun
          temp_int = abs(temp_int); //muutetan temp_int positiiviseksi luvuksi
          bufferi[1]=temp_int%1000/100;  //selvitetään ensimmäinen numero
          bufferi[2]=temp_int%100/10;  //selvitetään toinen numero
          bufferi[3]=temp_int%10; // selvitetään viimeinen numero
          if (bufferi[1]== 0){  // jos ensimmäinen numero on 0 näytetään tyhjää
            bufferi[1]= 10;
          }
        }
      }else{//positiiviset lämpötilat
        bufferi[0]=temp_int/1000; // selvitetään ensimmäinen numero
        bufferi[1]=temp_int%1000/100;  //selvitetään toinen numero
        bufferi[2]=temp_int%100/10;  //selvitetään kolmas numero
        bufferi[3]=temp_int%10; //selvitetään viimeinen numero
        if (bufferi[0]== 0){ // jos ensimmäinen numero on 0 näytetään tyhjää
          bufferi[0]= 10;
        }
        if (bufferi[1]== 0){ // jos toinen numero on 0 näytetään tyhjää
          bufferi[1]= 10;
        }
      }
      
      //Käsketään anturia aloittamaan seuraavan lämpötilan mittaus, tulos luetaan seuraavalla kerralla
      sensors.requestTemperatures();
    }
  }
  
  //********************************************************************************
  // Valikko "On" , menu_state = 1
  //********************************************************************************
  else if(menu_state == 1){
    //Ohjelman asetusvalikko on aktivoitu ja "On" näytetään näytöllä
    bufferi[0]=10; //" "
    bufferi[1]=0;  //O
    bufferi[2]=12; //n 
    bufferi[3]=10; //" "
    //Jos S1/+ nappia painetaan siirrytään seuraavaan valikkoon (menu_state = 2)
    if (digitalRead(S1)== LOW){
      menu_state = 2;
      //Odotellaan että käyttäjä vapauttaa painikeen, mutta päivitetään näyttö kuitenkin sillä välin (muuten näyttö olisi pimeä kun painonappi on pohjassa)
      while (digitalRead(S1)== LOW){
        display();
      }
    }
    //Jos S2/- nappia painetaan siirrytään edelliseen valikkoon (menu_state = 3)
    else if (digitalRead(S2)== LOW){
      menu_state = 3;
      //Odotellaan että käyttäjä vapauttaa painikeen, mutta päivitetään näyttö kuitenkin sillä välin (muuten näyttö olisi pimeä kun painonappi on pohjassa)
      while (digitalRead(S2)== LOW){
        display();
      }
    }
    //Jos S3/enter nappia painetaan siirrytään muuttamaan on_temp arvoa (menu_state = 4)
    else if (digitalRead(S3)== LOW){
      menu_state = 4;
      //Odotellaan että käyttäjä vapauttaa painikeen, mutta päivitetään näyttö kuitenkin sillä välin (muuten näyttö olisi pimeä kun painonappi on pohjassa)
      while (digitalRead(S3)== LOW){
        display();
      }
    }
  }
  
  //********************************************************************************
  // Valikko "Off" , menu_state = 2
  //********************************************************************************
  else if(menu_state == 2){
    //Ohjelman asetusvalikko on aktivoitu ja "Off" näytetään näytöllä
    bufferi[0]=10; //" "
    bufferi[1]=0;  //O
    bufferi[2]=13; //F 
    bufferi[3]=13; //F
    //Jos S1/+ nappia painetaan siirrytään seuraavaan valikkoon (menu_state = 3)
    if (digitalRead(S1)== LOW){
      menu_state = 3;
      //Odotellaan että käyttäjä vapauttaa painikeen, mutta päivitetään näyttö kuitenkin sillä välin (muuten näyttö olisi pimeä kun painonappi on pohjassa)
      while (digitalRead(S1)== LOW){
        display();
      }
    }
    //Jos S2/- nappia painetaan siirrytään edelliseen valikkoon (menu_state = 2)
    else if (digitalRead(S2)== LOW){
      menu_state = 1;
      //Odotellaan että käyttäjä vapauttaa painikeen, mutta päivitetään näyttö kuitenkin sillä välin (muuten näyttö olisi pimeä kun painonappi on pohjassa)
      while (digitalRead(S2)== LOW){
        display();
      }
    }
    //Jos S3/enter nappia painetaan siirrytään muuttamaan off_temp arvoa (menu_state = 5)
    else if (digitalRead(S3)== LOW){
      menu_state = 5;
      //Odotellaan että käyttäjä vapauttaa painikeen, mutta päivitetään näyttö kuitenkin sillä välin (muuten näyttö olisi pimeä kun painonappi on pohjassa)
      while (digitalRead(S3)== LOW){
        display();
      }
    }
  }

  //********************************************************************************
  // Valikko "ret" , menu_state = 3
  //********************************************************************************
  else if (menu_state == 3){
    //Ohjelman asetusvalikko on aktivoitu ja "ret" näytetään näytöllä
    bufferi[0]=10; //" "
    bufferi[1]=14; //r
    bufferi[2]=15; //E 
    bufferi[3]=7;  //t
    //Jos S1/+ nappia painetaan siirrytään seuraavaan valikkoon (menu_state = 1)
    if (digitalRead(S1)== LOW){
      menu_state = 1;
      //Odotellaan että käyttäjä vapauttaa painikeen, mutta päivitetään näyttö kuitenkin sillä välin (muuten näyttö olisi pimeä kun painonappi on pohjassa)
      while (digitalRead(S1)== LOW){
        display();
      }
    }
    //Jos S2/- nappia painetaan siirrytään edelliseen valikkoon (menu_state = 2)
    else if (digitalRead(S2)== LOW){
      menu_state = 2;
      //Odotellaan että käyttäjä vapauttaa painikeen, mutta päivitetään näyttö kuitenkin sillä välin (muuten näyttö olisi pimeä kun painonappi on pohjassa)
      while (digitalRead(S2)== LOW){
        display();
      }
    }
    //Jos S3/enter nappia painetaan siirrytään takaisin normaaliin toimintaan (menu_state = 0)
    else if (digitalRead(S3)== LOW){
      menu_state = 0;
      //Ennen kuin lämpötilanäyttö päivittyy näytetään ruudulla - - - -, jotta käyttäjä huomaa että napin painallus teki jotain
      bufferi[0]=11; //-
      bufferi[1]=11; //-
      bufferi[2]=11; //-
      bufferi[3]=11; //-
      //Tallennetaan valikoissa muutetut on_temp ja off_temp arvot Arduinon sisäiseen EEPROM muistiin
      // .put() komento tallentaa uuden arvon vain jos se on muuttunut
      EEPROM.put(0,on_temp);
      EEPROM.put(32,off_temp);
      //Odotellaan että käyttäjä vapauttaa painikeen, mutta päivitetään näyttö kuitenkin sillä välin (muuten näyttö olisi pimeä kun painonappi on pohjassa)
      while (digitalRead(S3)== LOW){
        display();
      }
    }
  }

  //********************************************************************************
  // on_temp muuttaminen , menu_state = 4
  //********************************************************************************
  else if (menu_state == 4){
    //Ohjelma antaa käyttäjän muuttaa on_temp arvoa ruudulla näytettään on_temp nykyinen arvo
    //Tarkistetaan onko näytettävä numero negatiivinen
    if (on_temp <0){
      // negatiivinen
      // näytetään ensimmäisenä merrkinä -
      bufferi[0]= 11; //-
      // tallennetaan väliaikaiseen muuttujaan näytettävän numeron itseisarvo laskutoimitusten helpottamiseksi
      temp_int = abs(on_temp);
      bufferi[1]=temp_int%1000/100;  //selvitetään ensimmäinen nnumero
      bufferi[2]=temp_int%100/10;   //selvitetään toinen numero
      bufferi[3]=temp_int%10; // selvitetään viimeinen numero
      if (bufferi[1]== 0){ // mikäli ensimmäinen numero on 0, näytetään tyhjää
        bufferi[1]= 10;
      }
    }else{
      //positiivinen
      
      bufferi[0]=on_temp/1000; //selvitetään ensimmäinen nnumero
      bufferi[1]=on_temp%1000/100;  //selvitetään toinen nnumero
      bufferi[2]=on_temp%100/10;  //selvitetään kolmas nnumero
      bufferi[3]=on_temp%10; //selvitetään viimeinen nnumero
      if (bufferi[0]== 0){  // mikäli ensimmäinen numero on 0, näytetään tyhjää
        bufferi[0]= 10;
      }
      if (bufferi[1]== 0){  // mikäli toinen numero on 0, näytetään tyhjää
        bufferi[1]= 10;
      }
    }
    //mikäli S1/+ painiketta on painettu kasvatetaan on_temp arvoa
    if (digitalRead(S1)== LOW){
      on_temp++;
      if (on_temp>1280){ // varmistetaan että on_temp ei kasva yli sallitun maksimin 128C
        on_temp = 1280;
      }
      //Pitkän painalluksen käsittely
      //nollataan laskuri
      speed_increace_timer = 0;
      //Toistetaan while silmukkaa niin kauan kuin nappi on pohjassa
      while (digitalRead(S1)== LOW){
        //jos laskuri on vielä liian pieni, kasvatetaan laskurin arvoa ja päivitetään näyttöä, mutta ei tehdä muuta
        if (speed_increace_timer < SPEED_INCREACE_DELAY){
          speed_increace_timer++;
        // laskuri saavuttanut tavoitearvon (while silmukkaa on toistettu riittävän kauan, riittävä viive saavutettu)
        }else{
          //kasvatetaan on_temp arvoa, joka while silmukan kierroksella
          on_temp++;
          if (on_temp>1280){ // varmistetaan ettei on_temp ylitä maksimiarvoa 128C
            on_temp = 1280;
          }
          //Tarkistetaan onko näytettävä numero negatiivinen
          if (on_temp <0){
            // negatiivinen
            // näytetään ensimmäisenä merrkinä -
            bufferi[0]= 11; //-
            // tallennetaan väliaikaiseen muuttujaan näytettävän numeron itseisarvo laskutoimitusten helpottamiseksi
            temp_int = abs(on_temp);
            bufferi[1]=temp_int%1000/100;  //selvitetään ensimmäinen nnumero
            bufferi[2]=temp_int%100/10;   //selvitetään toinen numero
            bufferi[3]=temp_int%10; // selvitetään viimeinen numero
            if (bufferi[1]== 0){ // mikäli ensimmäinen numero on 0, näytetään tyhjää
              bufferi[1]= 10;
            }
          }else{
            //positiivinen
            
            bufferi[0]=on_temp/1000; //selvitetään ensimmäinen nnumero
            bufferi[1]=on_temp%1000/100;  //selvitetään toinen nnumero
            bufferi[2]=on_temp%100/10;  //selvitetään kolmas nnumero
            bufferi[3]=on_temp%10; //selvitetään viimeinen nnumero
            if (bufferi[0]== 0){  // mikäli ensimmäinen numero on 0, näytetään tyhjää
              bufferi[0]= 10;
            }
            if (bufferi[1]== 0){  // mikäli toinen numero on 0, näytetään tyhjää
              bufferi[1]= 10;
            }
          }
        }
        //Päivitetään näyttöä silmukans sisällä, jotta numerot vaihtuvat pikakelauksen aikana
        display();
      }//while silmukan loppu
    }
    //Mikäli S2/- on painettu pienennetään on_temp arvoa
    else if (digitalRead(S2)== LOW){
      on_temp--;
       if (on_temp<-550){ //varmistetaan ettei on_temp alita minimi arvoa -55C
        on_temp = -550;
      }
      //Pitkän painalluksen käsittely
      //nollataan laskuri
      speed_increace_timer = 0;
      //Toistetaan while silmukkaa niin kauan kuin nappi on pohjassa
      while (digitalRead(S2)== LOW){
        //jos laskuri on vielä liian pieni, kasvatetaan laskurin arvoa ja päivitetään näyttöä, mutta ei tehdä muuta
        if (speed_increace_timer < SPEED_INCREACE_DELAY){
          speed_increace_timer++;
        // laskuri saavuttanut tavoitearvon (while silmukkaa on toistettu riittävän kauan, riittävä viive saavutettu)
        }else{
          //pienennetään on_temp arvoa, joka while silmukan kierroksella
          on_temp--;
          if (on_temp<-550){ //varmistetaan ettei on_temp alita minimi arvoa -55C
            on_temp = -550;
          }
          //Tarkistetaan onko näytettävä numero negatiivinen
          if (on_temp <0){
            // negatiivinen
            // näytetään ensimmäisenä merrkinä -
            bufferi[0]= 11; //-
            // tallennetaan väliaikaiseen muuttujaan näytettävän numeron itseisarvo laskutoimitusten helpottamiseksi
            temp_int = abs(on_temp);
            bufferi[1]=temp_int%1000/100;  //selvitetään ensimmäinen nnumero
            bufferi[2]=temp_int%100/10;   //selvitetään toinen numero
            bufferi[3]=temp_int%10; // selvitetään viimeinen numero
            if (bufferi[1]== 0){ // mikäli ensimmäinen numero on 0, näytetään tyhjää
              bufferi[1]= 10;
            }
          }else{
            //positiivinen
            
            bufferi[0]=on_temp/1000; //selvitetään ensimmäinen nnumero
            bufferi[1]=on_temp%1000/100;  //selvitetään toinen nnumero
            bufferi[2]=on_temp%100/10;  //selvitetään kolmas nnumero
            bufferi[3]=on_temp%10; //selvitetään viimeinen nnumero
            if (bufferi[0]== 0){  // mikäli ensimmäinen numero on 0, näytetään tyhjää
              bufferi[0]= 10;
            }
            if (bufferi[1]== 0){  // mikäli toinen numero on 0, näytetään tyhjää
              bufferi[1]= 10;
            }
          }
        }
        //Päivitetään näyttöä silmukans sisällä, jotta numerot vaihtuvat pikakelauksen aikana
        display();
      }// while silmukan loppu

    //mikäli S3/enter painettu siirrytään takaisin "On" valikkoon menu_state = 1
    }else if (digitalRead(S3)== LOW){
      menu_state = 1;
      if (off_temp == on_temp){//mikäli asetettu on_temp on täysin sama kuin off_temp, nostetaan on_temp arvoa 0,1C jottei tule ongelmia
        on_temp++;
      }
      //odotellaan että käyttjä vapauttaa painikkeen
      while (digitalRead(S3)== LOW){
        //Päivitetään näyttöä silmukans sisällä, jotta näyttö ei olisi pimeä
        display();
      }
    }
  }else{
    //Ohjelma antaa käyttäjän muuttaa on_temp arvoa ruudulla näytettään off_temp nykyinen arvo
    //Tarkistetaan onko näytettävä numero negatiivinen
    if (off_temp <0){
      // negatiivinen
      // näytetään ensimmäisenä merrkinä -
      bufferi[0]= 11;
      // tallennetaan väliaikaiseen muuttujaan näytettävän numeron itseisarvo laskutoimitusten helpottamiseksi
      temp_int = abs(off_temp);
      bufferi[1]=temp_int%1000/100;  //selvitetään ensimmäinen nnumero
      bufferi[2]=temp_int%100/10;  //selvitetään toinen nnumero
      bufferi[3]=temp_int%10;  //selvitetään viimeinen nnumero
      if (bufferi[1]== 0){ // mikäli ensimmäinen numero on 0, näytetään tyhjää
        bufferi[1]= 10;
      }
    }else{
      bufferi[0]=off_temp/1000;  //selvitetään ensimmäinen nnumero
      bufferi[1]=off_temp%1000/100; //selvitetään toinen nnumero
      bufferi[2]=off_temp%100/10;  //selvitetään kolmas nnumero
      bufferi[3]=off_temp%10;  //selvitetään viimeinen nnumero
      if (bufferi[0]== 0){ // mikäli ensimmäinen numero on 0, näytetään tyhjää
        bufferi[0]= 10;
      }
      if (bufferi[1]== 0){  // mikäli toinen numero on 0, näytetään tyhjää
        bufferi[1]= 10;
      }
    }
    //mikäli S1/+ painiketta on painettu kasvatetaan off_temp arvoa
    if (digitalRead(S1)== LOW){
      off_temp++;
      if (off_temp>1280){ // varmistetaan että off_temp ei kasva yli sallitun maksimin 128C
        off_temp = 1280;
      }
      //Pitkän painalluksen käsittely
      //nollataan laskuri
      speed_increace_timer = 0;
      //Toistetaan while silmukkaa niin kauan kuin nappi on pohjassa
      while (digitalRead(S1)== LOW){
        //jos laskuri on vielä liian pieni, kasvatetaan laskurin arvoa ja päivitetään näyttöä, mutta ei tehdä muuta
        if (speed_increace_timer < SPEED_INCREACE_DELAY){
          speed_increace_timer++;
        // laskuri saavuttanut tavoitearvon (while silmukkaa on toistettu riittävän kauan, riittävä viive saavutettu)
        }else{
          //kasvatetaan off_temp arvoa, joka while silmukan kierroksella
          off_temp++;
          if (off_temp>1280){ // varmistetaan ettei off_temp ylitä maksimiarvoa 128C
            off_temp = 1280;
          }
          //Tarkistetaan onko näytettävä numero negatiivinen
          if (off_temp <0){
            // negatiivinen
            // näytetään ensimmäisenä merrkinä -
            bufferi[0]= 11; //-
            // tallennetaan väliaikaiseen muuttujaan näytettävän numeron itseisarvo laskutoimitusten helpottamiseksi
            temp_int = abs(off_temp);
            bufferi[1]=temp_int%1000/100;  //selvitetään ensimmäinen nnumero
            bufferi[2]=temp_int%100/10;   //selvitetään toinen numero
            bufferi[3]=temp_int%10; // selvitetään viimeinen numero
            if (bufferi[1]== 0){ // mikäli ensimmäinen numero on 0, näytetään tyhjää
              bufferi[1]= 10;
            }
          }else{
            //positiivinen
            
            bufferi[0]=off_temp/1000; //selvitetään ensimmäinen nnumero
            bufferi[1]=off_temp%1000/100;  //selvitetään toinen nnumero
            bufferi[2]=off_temp%100/10;  //selvitetään kolmas nnumero
            bufferi[3]=off_temp%10; //selvitetään viimeinen nnumero
            if (bufferi[0]== 0){  // mikäli ensimmäinen numero on 0, näytetään tyhjää
              bufferi[0]= 10;
            }
            if (bufferi[1]== 0){  // mikäli toinen numero on 0, näytetään tyhjää
              bufferi[1]= 10;
            }
          }
        }
        //Päivitetään näyttöä silmukans sisällä, jotta numerot vaihtuvat pikakelauksen aikana
        display();
      }//while silmukan loppu
    }
    //Mikäli S2/- on painettu pienennetään off_temp arvoa
    else if (digitalRead(S2)== LOW){
      off_temp--;
       if (off_temp<-550){ //varmistetaan ettei off_temp alita minimi arvoa -55C
        off_temp = -550;
      }
      //Pitkän painalluksen käsittely
      //nollataan laskuri
      speed_increace_timer = 0;
      //Toistetaan while silmukkaa niin kauan kuin nappi on pohjassa
      while (digitalRead(S2)== LOW){
        //jos laskuri on vielä liian pieni, kasvatetaan laskurin arvoa ja päivitetään näyttöä, mutta ei tehdä muuta
        if (speed_increace_timer < SPEED_INCREACE_DELAY){
          speed_increace_timer++;
        // laskuri saavuttanut tavoitearvon (while silmukkaa on toistettu riittävän kauan, riittävä viive saavutettu)
        }else{
          //pienennetään off_temp arvoa, joka while silmukan kierroksella
          off_temp--;
          if (off_temp<-550){ //varmistetaan ettei off_temp alita minimi arvoa -55C
            off_temp = -550;
          }
          //Tarkistetaan onko näytettävä numero negatiivinen
          if (off_temp <0){
            // negatiivinen
            // näytetään ensimmäisenä merrkinä -
            bufferi[0]= 11; //-
            // tallennetaan väliaikaiseen muuttujaan näytettävän numeron itseisarvo laskutoimitusten helpottamiseksi
            temp_int = abs(off_temp);
            bufferi[1]=temp_int%1000/100;  //selvitetään ensimmäinen nnumero
            bufferi[2]=temp_int%100/10;   //selvitetään toinen numero
            bufferi[3]=temp_int%10; // selvitetään viimeinen numero
            if (bufferi[1]== 0){ // mikäli ensimmäinen numero on 0, näytetään tyhjää
              bufferi[1]= 10;
            }
          }else{
            //positiivinen
            
            bufferi[0]=off_temp/1000; //selvitetään ensimmäinen nnumero
            bufferi[1]=off_temp%1000/100;  //selvitetään toinen nnumero
            bufferi[2]=off_temp%100/10;  //selvitetään kolmas nnumero
            bufferi[3]=off_temp%10; //selvitetään viimeinen nnumero
            if (bufferi[0]== 0){  // mikäli ensimmäinen numero on 0, näytetään tyhjää
              bufferi[0]= 10;
            }
            if (bufferi[1]== 0){  // mikäli toinen numero on 0, näytetään tyhjää
              bufferi[1]= 10;
            }
          }
        }
        //Päivitetään näyttöä silmukans sisällä, jotta numerot vaihtuvat pikakelauksen aikana
        display();
      }// while silmukan loppu
    }
    //mikäli S3/enter painettu siirrytään takaisin "Off" valikkoon menu_state = 2
    else if (digitalRead(S3)== LOW){
      menu_state = 2;
      if (off_temp == on_temp){//mikäli asetettu off_temp on täysin sama kuin on_temp, nostetaan off_temp arvoa 0,1C jottei tule ongelmia
        off_temp++;
      }
      //odotellaan että käyttjä vapauttaa painikkeen
      while (digitalRead(S3)== LOW){
        //Päivitetään näyttöä silmukans sisällä, jotta näyttö ei olisi pimeä
        display();
      }
    }
  }// menu_state valinnan loppu

  //pävitetään näyttöä jokaisella void loop() silmukan kierroksella, jotta näyttö ei vilkkuisi
  display();
}
