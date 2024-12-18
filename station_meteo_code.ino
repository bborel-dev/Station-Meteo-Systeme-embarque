void configurationMode(); // Déclaration de la fonction de configuration
#include <Wire.h>
#include <SD.h>
#include <RTClib.h>
#include <Adafruit_BME280.h>
#include <ChainableLED.h>
#include <EEPROM.h>

// Déclaration des broches
const int boutonRougePin = 2;  // Grove Dual Button (bouton rouge)
const int boutonVertPin = 3;   // Grove Dual Button (bouton vert)
const int lightSensorPin = A0; // Grove Light Sensor
const int chipSelect = 4;      // Pin CS pour la carte SD

// LED RGB chainable Grove branchée sur D4 et D5
ChainableLED leds(4, 5, 1);

// Capteur BME280
Adafruit_BME280 bme;

// RTC pour l'horloge temps réel
RTC_DS1307 rtc;

// Variables globales

// Configuration des paramètres
int LUMIN = 1;                 // Activation du capteur de luminosité (0 = off, 1 = on)
int LUMIN_LOW = 255;           // Seuil de luminosité "faible"
int LUMIN_HIGH = 768;          // Seuil de luminosité "forte"

int TEMP_AIR = 1;              // Activation du capteur de température de l'air (0 = off, 1 = on)
int MIN_TEMP_AIR = -10;        // Température minimale avant mise en erreur
int MAX_TEMP_AIR = 60;         // Température maximale avant mise en erreur

int HYGR = 1;                  // Activation du capteur d'hygrométrie (0 = off, 1 = on)
int HYGR_MINT = 0;             // Température minimale pour hygrométrie
int HYGR_MAXT = 50;            // Température maximale pour hygrométrie

int PRESSURE = 1;              // Activation du capteur de pression (0 = off, 1 = on)
int PRESSURE_MIN = 850;        // Pression minimale en HPa
int PRESSURE_MAX = 1080;       // Pression maximale en HPa

String mode = "standard";
String lastMode = "standard";
bool enErreur = false;  // Indique si le système est en mode erreur
unsigned long tempsInactivite = 0;
const unsigned long INACTIVITE_TIMER = 30000; // 30 secondes
const unsigned long LOG_INTERVAL = 10000; // 10 secondes
unsigned long dernier_acquisition = 0;
const unsigned long Delay_anti_rebond = 50;
const unsigned long attenteLong = 5000;
const unsigned long antiRebondDelay = 500;

bool boutonRougeEtat = HIGH;
bool boutonRougeDernierEtat = HIGH;
unsigned long dernierTempsRouge = 0;
unsigned long tempsAppuiRouge = 0;
bool rougeAppuiEnCours = false;

bool boutonVertEtat = HIGH;
bool boutonVertDernierEtat = HIGH;
unsigned long dernierTempsVert = 0;
unsigned long tempsAppuiVert = 0;
bool vertAppuiEnCours = false;

unsigned long dernier_changement_etat = 0;
bool changement_etat = false;

unsigned long dernierLogMaintenance = 0;
const unsigned long MAINTENANCE_INTERVAL = 10000;

unsigned long intervalAcquisition = LOG_INTERVAL;

bool messageEconomieAffiche = false;

// Clignotement d'erreur sans boucle bloquante
void clignoter_LED(int red1, int green1, int blue1, int red2, int green2, int blue2) {
    unsigned long currentMillis = millis();
    if (currentMillis - dernier_changement_etat >= 500) {
        dernier_changement_etat = currentMillis;
        changement_etat = !changement_etat;
        if (changement_etat) {
            leds.setColorRGB(0, red1, green1, blue1);
        } else {
            leds.setColorRGB(0, red2, green2, blue2);
        }
    }
}

// Initialisation du système
void setup() {
    Serial.begin(9600);
    leds.init();
    leds.setColorRGB(0, 0, 255, 0);  // LED verte, système prêt
    pinMode(boutonRougePin, INPUT_PULLUP);
    pinMode(boutonVertPin, INPUT_PULLUP);

    loadFromEEPROM();
}

// Bascule entre les modes
void basculerMode(String nouveauMode) {
    lastMode = mode;
    mode = nouveauMode;
    Serial.println("Changement de mode: " + mode);
    rougeAppuiEnCours = false;
    boutonRougeEtat = HIGH;
    boutonRougeDernierEtat = HIGH;
    dernierTempsRouge = millis();
    vertAppuiEnCours = false;
    boutonVertEtat = HIGH;
    boutonVertDernierEtat = HIGH;
    dernierTempsVert = millis();
    delay(antiRebondDelay);

    if (mode == "maintenance") {
        leds.setColorRGB(0, 255, 165, 0);
    } else if (mode == "économique") {
        leds.setColorRGB(0, 0, 0, 255);
    } else if (mode == "configuration") {
        leds.setColorRGB(0, 255, 255, 0);
        tempsInactivite = millis();
    } else {
        leds.setColorRGB(0, 0, 255, 0);
    }
}

// Acquisition des données des capteurs
void acquisition_donnee() {
    String donnee = "";
    float temperature = bme.readTemperature();
    float pression = bme.readPressure() / 100.0F;
    float humidite = bme.readHumidity();
    donnee += "Température: " + String(temperature) + " *C, ";
    donnee += "Pression: " + String(pression) + " hPa, ";
    donnee += "Humidité: " + String(humidite) + " %, ";
    int luminosite = analogRead(lightSensorPin);
    donnee += "Luminosité: " + String(luminosite) + ", ";
    Serial.println(donnee);
    sauvegarder_SD(donnee);
}

// Sauvegarde sur la carte SD
void sauvegarder_SD(String data) {
    File fichierSD = SD.open("log.txt", FILE_WRITE);
    if (fichierSD) {
        fichierSD.println(data);
        fichierSD.close();
        Serial.println("Données sauvegardées sur la carte SD.");
    } else {
        Serial.println("Erreur d'écriture sur la carte SD.");
        enErreur = true;
        clignoter_LED(255, 0, 0, 255, 255, 255);
    }
}

// Fonction principale

void loop() {
    unsigned long currentMillis = millis();

    // Vérification des capteurs et gestion des erreurs
    if (!bme.begin(0x76)) {
        Serial.println("Erreur BME280 !");
        enErreur = true;
        clignoter_LED(255, 0, 0, 0, 255, 0);
    } else if (!rtc.begin()) {
        Serial.println("Erreur RTC !");
        enErreur = true;
        clignoter_LED(255, 0, 0, 0, 0, 255);
    } else if (!SD.begin(chipSelect) && mode != "maintenance") {
        Serial.println("Erreur initialisation carte SD");
        enErreur = true;
        clignoter_LED(255, 0, 0, 255, 255, 255);
    } else {
        if (enErreur) {
            enErreur = false;
            basculerMode("standard");
        }
    }

    if (enErreur) return; 

    if (mode == "configuration") {
        configurationMode();  // Appelle la fonction de configuration
        basculerMode("standard");  // Retourne automatiquement en mode standard après configuration
        return;
    }
    if (mode == "maintenance"){
      mode_Maintenance();
      return;
    }
    if (mode == "économique"){
      mode_Economique();
    }

    // Lecture des états des boutons
    bool lectureBoutonRouge = digitalRead(boutonRougePin);
    bool lectureBoutonVert = digitalRead(boutonVertPin);

    if (lectureBoutonRouge != boutonRougeDernierEtat) {
        dernierTempsRouge = currentMillis;
        boutonRougeDernierEtat = lectureBoutonRouge;
    }

    if ((currentMillis - dernierTempsRouge) > Delay_anti_rebond) {
        if (lectureBoutonRouge != boutonRougeEtat) {
            boutonRougeEtat = lectureBoutonRouge;

            if (boutonRougeEtat == LOW) {
                rougeAppuiEnCours = true;
                tempsAppuiRouge = currentMillis;
            } else if (boutonRougeEtat == HIGH && rougeAppuiEnCours) {
                rougeAppuiEnCours = false;
                unsigned long dureeAppuiRouge = currentMillis - tempsAppuiRouge;

                if (dureeAppuiRouge >= attenteLong) {
                    if (mode == "maintenance") basculerMode("standard");
                    else if (mode == "standard") basculerMode("maintenance");
                    else if (mode == "économique") basculerMode("standard");
                } else if (mode == "standard") basculerMode("configuration");
            }
        }
    }

    if (lectureBoutonVert != boutonVertDernierEtat) {
        dernierTempsVert = currentMillis;
        boutonVertDernierEtat = lectureBoutonVert;
    }

    if ((currentMillis - dernierTempsVert) > Delay_anti_rebond) {
        if (lectureBoutonVert != boutonVertEtat) {
            boutonVertEtat = lectureBoutonVert;

            if (boutonVertEtat == LOW) {
                vertAppuiEnCours = true;
                tempsAppuiVert = currentMillis;
            } else if (boutonVertEtat == HIGH && vertAppuiEnCours) {
                vertAppuiEnCours = false;
                unsigned long dureeAppuiVert = currentMillis - tempsAppuiVert;

                if (dureeAppuiVert >= attenteLong && mode == "standard") {
                    basculerMode("économique");
                }
            }
        }
    }

    if (mode == "standard" || mode == "économique") {
        if (currentMillis - dernier_acquisition >= intervalAcquisition) {
            acquisition_donnee();
            dernier_acquisition = currentMillis;
        }
    }

    delay(50); // Délai pour éviter la surcharge du processeur
}

int getIntInput(int currentVal) {
    while (Serial.available() == 0) {} // Attend l'entrée
    int val = Serial.parseInt();
    if (val == 0 && Serial.read() == '0') { // Permet de détecter le 0 si saisi
        return 0;
    }
    return (val != 0) ? val : currentVal; // Retourne la nouvelle valeur si elle est valide, sinon conserve l'ancienne
}
// Fonction pour lire une chaîne de caractères depuis la console
String getStringInput(String defaultVal) {
    while (Serial.available() == 0) {} // Attend l'entrée
    String input = Serial.readString();
    input.trim();
    return (input.length() > 0) ? input : defaultVal;
}
// Convertit le jour de la semaine (String) en entier pour le RTC
int Jourdelasemaine_Entier(String jour) {
    if (jour == "MON") return 1;
    if (jour == "TUE") return 2;
    if (jour == "WED") return 3;
    if (jour == "THU") return 4;
    if (jour == "FRI") return 5;
    if (jour == "SAT") return 6;
    if (jour == "SUN") return 7;
    return 1; // Par défaut Lundi (MON)
}

void configurationMode() {
    Serial.println(F("=== Mode Configuration ==="));
    
     // Configuration de l'heure (CLOCK)
    Serial.println(F("Configuration de l'heure du jour au format HEURE:MINUTE:SECONDE"));
    Serial.print(F("Entrez l'heure (0-23) : "));
    int heure = getIntInput(12); // Valeur par défaut 12
    delay(5000);
    Serial.print(F("Entrez les minutes (0-59) : "));
    int minute = getIntInput(0); // Valeur par défaut 0
    delay(5000);
    Serial.print(F("Entrez les secondes (0-59) : "));
    int seconde = getIntInput(0); // Valeur par défaut 0
    delay(5000);
    rtc.adjust(DateTime(2023, 1, 1, heure, minute, seconde));

    // Configuration de la date (DATE)
    Serial.println(F("Configuration de la date au format MOIS,JOUR,ANNEE"));
    Serial.print(F("Entrez le mois (1-12) : "));
    int mois = getIntInput(1); // Valeur par défaut 1 (Janvier)
    delay(5000);
    Serial.print(F("Entrez le jour (1-31) : "));
    int jour = getIntInput(1); // Valeur par défaut 1
    delay(5000);
    Serial.print(F("Entrez l'année (2000-2099) : "));
    int annee = getIntInput(2023); // Valeur par défaut 2023
    delay(5000);
    rtc.adjust(DateTime(annee, mois, jour, heure, minute, seconde)); // Met à jour la date et l'heure complètes

    // Configuration du jour de la semaine (DAY)
    Serial.println(F("Configuration du jour de la semaine (MON, TUE, WED, THU, FRI, SAT, SUN)"));
    Serial.print(F("Entrez le jour de la semaine : "));
    String jourSemaine = getStringInput("MON"); // Valeur par défaut MON (lundi)
    delay(5000);
    
    int jourSemaineRTC = Jourdelasemaine_Entier(jourSemaine); // Fonction pour convertir en entier 

    Serial.println(F("Configuration terminée. Sauvegarde dans EEPROM..."));
    sauvegarder_EEPROM();
    Serial.println(F("Les paramètres sont bien sauvegardés dans l'EEPROM."));

    // Configuration du capteur de luminosité
    Serial.print(F("Activer capteur de luminosité (0=off, 1=on) [LUMIN="));
    Serial.print(LUMIN);
    Serial.println(F("] : "));
    LUMIN = getIntInput(LUMIN);

    Serial.print(F("Seuil de luminosité faible [LUMIN_LOW="));
    Serial.print(LUMIN_LOW);
    Serial.println(F("] : "));
    LUMIN_LOW = getIntInput(LUMIN_LOW);

    Serial.print(F("Seuil de luminosité forte [LUMIN_HIGH="));
    Serial.print(LUMIN_HIGH);
    Serial.println(F("] : "));
    LUMIN_HIGH = getIntInput(LUMIN_HIGH);

    // Configuration du capteur de température de l'air
    Serial.print(F("Activer capteur de température de l'air (0=off, 1=on) [TEMP_AIR="));
    Serial.print(TEMP_AIR);
    Serial.println(F("] : "));
    TEMP_AIR = getIntInput(TEMP_AIR);

    Serial.print(F("Température minimale pour mise en erreur [MIN_TEMP_AIR="));
    Serial.print(MIN_TEMP_AIR);
    Serial.println(F("] : "));
    MIN_TEMP_AIR = getIntInput(MIN_TEMP_AIR);

    Serial.print(F("Température maximale pour mise en erreur [MAX_TEMP_AIR="));
    Serial.print(MAX_TEMP_AIR);
    Serial.println(F("] : "));
    MAX_TEMP_AIR = getIntInput(MAX_TEMP_AIR);

    // Configuration du capteur d'hygrométrie
    Serial.print(F("Activer capteur d'hygrométrie (0=off, 1=on) [HYGR="));
    Serial.print(HYGR);
    Serial.println(F("] : "));
    HYGR = getIntInput(HYGR);

    Serial.print(F("Température minimale pour hygrométrie [HYGR_MINT="));
    Serial.print(HYGR_MINT);
    Serial.println(F("] : "));
    HYGR_MINT = getIntInput(HYGR_MINT);

    Serial.print(F("Température maximale pour hygrométrie [HYGR_MAXT="));
    Serial.print(HYGR_MAXT);
    Serial.println(F("] : "));
    HYGR_MAXT = getIntInput(HYGR_MAXT);

    // Configuration du capteur de pression
    Serial.print(F("Activer capteur de pression (0=off, 1=on) [PRESSURE="));
    Serial.print(PRESSURE);
    Serial.println(F("] : "));
    PRESSURE = getIntInput(PRESSURE);

    Serial.print(F("Pression minimale (en HPa) [PRESSURE_MIN="));
    Serial.print(PRESSURE_MIN);
    Serial.println(F("] : "));
    PRESSURE_MIN = getIntInput(PRESSURE_MIN);

    Serial.print(F("Pression maximale (en HPa) [PRESSURE_MAX="));
    Serial.print(PRESSURE_MAX);
    Serial.println(F("] : "));
    PRESSURE_MAX = getIntInput(PRESSURE_MAX);

    Serial.println(F("Configuration terminée. Sauvegarde dans EEPROM..."));
    sauvegarder_EEPROM();
    Serial.println(F("Les paramètres sont bien sauvegardés dans l'EEPROM."));
}

void sauvegarder_EEPROM() {
  int addr = 0;
    EEPROM.put(addr, LUMIN); addr += sizeof(LUMIN);
    EEPROM.put(addr, LUMIN_LOW); addr += sizeof(LUMIN_LOW);
    EEPROM.put(addr, LUMIN_HIGH); addr += sizeof(LUMIN_HIGH);

    EEPROM.put(addr, TEMP_AIR); addr += sizeof(TEMP_AIR);
    EEPROM.put(addr, MIN_TEMP_AIR); addr += sizeof(MIN_TEMP_AIR);
    EEPROM.put(addr, MAX_TEMP_AIR); addr += sizeof(MAX_TEMP_AIR);

    EEPROM.put(addr, HYGR); addr += sizeof(HYGR);
    EEPROM.put(addr, HYGR_MINT); addr += sizeof(HYGR_MINT);
    EEPROM.put(addr, HYGR_MAXT); addr += sizeof(HYGR_MAXT);

    EEPROM.put(addr, PRESSURE); addr += sizeof(PRESSURE);
    EEPROM.put(addr, PRESSURE_MIN); addr += sizeof(PRESSURE_MIN);
    EEPROM.put(addr, PRESSURE_MAX); addr += sizeof(PRESSURE_MAX);
}
// Ajout de la fonction pour charger les configurations depuis l'EEPROM
void loadFromEEPROM() {
    int addr = 0;
    EEPROM.get(addr, LUMIN); addr += sizeof(LUMIN);
    EEPROM.get(addr, LUMIN_LOW); addr += sizeof(LUMIN_LOW);
    EEPROM.get(addr, LUMIN_HIGH); addr += sizeof(LUMIN_HIGH);

    EEPROM.get(addr, TEMP_AIR); addr += sizeof(TEMP_AIR);
    EEPROM.get(addr, MIN_TEMP_AIR); addr += sizeof(MIN_TEMP_AIR);
    EEPROM.get(addr, MAX_TEMP_AIR); addr += sizeof(MAX_TEMP_AIR);

    EEPROM.get(addr, HYGR); addr += sizeof(HYGR);
    EEPROM.get(addr, HYGR_MINT); addr += sizeof(HYGR_MINT);
    EEPROM.get(addr, HYGR_MAXT); addr += sizeof(HYGR_MAXT);

    EEPROM.get(addr, PRESSURE); addr += sizeof(PRESSURE);
    EEPROM.get(addr, PRESSURE_MIN); addr += sizeof(PRESSURE_MIN);
    EEPROM.get(addr, PRESSURE_MAX); addr += sizeof(PRESSURE_MAX);
}

void mode_Maintenance() {
    unsigned long currentMillis = millis();
    
    // Vérifie si l'intervalle de 10 secondes est écoulé pour l'affichage des données
    if (currentMillis - dernierLogMaintenance >= MAINTENANCE_INTERVAL) {
        dernierLogMaintenance = currentMillis;

        String donnee = "";
        float temperature = bme.readTemperature();
        float pression = bme.readPressure() / 100.0F;
        float humidite = bme.readHumidity();
        donnee += "Température: " + String(temperature) + " *C, ";
        donnee += "Pression: " + String(pression) + " hPa, ";
        donnee += "Humidité: " + String(humidite) + " %, ";
        int luminosite = analogRead(lightSensorPin);
        donnee += "Luminosité: " + String(luminosite) + ", ";
        
        Serial.println(donnee); // Affiche les données sans les enregistrer
    }

    // Gestion de l'appui long sur le bouton rouge pour retourner au mode standard
    bool lectureBoutonRouge = digitalRead(boutonRougePin);
    if (lectureBoutonRouge != boutonRougeDernierEtat) {
        dernierTempsRouge = currentMillis;
        boutonRougeDernierEtat = lectureBoutonRouge;
    }

    if ((currentMillis - dernierTempsRouge) > Delay_anti_rebond) {
        if (lectureBoutonRouge != boutonRougeEtat) {
            boutonRougeEtat = lectureBoutonRouge;

            if (boutonRougeEtat == LOW) {
                rougeAppuiEnCours = true;
                tempsAppuiRouge = currentMillis;
            } else if (boutonRougeEtat == HIGH && rougeAppuiEnCours) {
                rougeAppuiEnCours = false;
                unsigned long dureeAppuiRouge = currentMillis - tempsAppuiRouge;

                // Retourne au mode standard si l'appui est de 5 secondes ou plus
                if (dureeAppuiRouge >= attenteLong) {
                    basculerMode("standard");
                }
            }
        }
    }
}
void mode_Economique(){
  if(!messageEconomieAffiche){
      Serial.println("Mode économie de batterie activé ");
      Serial.println("Multiplication par 2 du temps d'acquisition des données ");
      intervalAcquisition = LOG_INTERVAL * 2;
      messageEconomieAffiche = true;
  }
  // Gestion de l'appui long sur le bouton rouge pour revenir au mode standard
    unsigned long currentMillis = millis();
    bool lectureBoutonRouge = digitalRead(boutonRougePin);

    if (lectureBoutonRouge != boutonRougeDernierEtat) {
        dernierTempsRouge = currentMillis;
        boutonRougeDernierEtat = lectureBoutonRouge;
    }

    if ((currentMillis - dernierTempsRouge) > Delay_anti_rebond) {
        if (lectureBoutonRouge != boutonRougeEtat) {
            boutonRougeEtat = lectureBoutonRouge;

            if (boutonRougeEtat == LOW) {
                rougeAppuiEnCours = true;
                tempsAppuiRouge = currentMillis;
            } else if (boutonRougeEtat == HIGH && rougeAppuiEnCours) {
                rougeAppuiEnCours = false;
                unsigned long dureeAppuiRouge = currentMillis - tempsAppuiRouge;

                // Si l'appui dure 5 secondes ou plus, bascule en mode standard
                if (dureeAppuiRouge >= attenteLong) {
                    basculerMode("standard");
                }
            }
        }
    }
}