// Uncomment the language you want to display on the screen
#define LANGUAGE_FR
//#define LANGUAGE_EN

#ifdef LANGUAGE_FR
// French weekday and month names for date formatting on display
const char *days[] = { "Dimanche", "Lundi", "Mardi", "Mercredi", "Jeudi", "Vendredi", "Samedi" };
const char *months[] = { "Janvier", "Février", "Mars", "Avril", "Mai", "Juin", "Juillet", "Août", "Septembre", "Octobre", "Novembre", "Décembre" };
const char *today_text = "Aujourd'hui";
const char *forecast_text = "Previsions";
const char *ha_sensor_text = "Capteurs Home Assistant";
const char *crypto_text = "Crypto";
const char *battery_text = "Batterie";
const char *indoor_text = "Interieur";
const char *outdoor_text = "Exterieur";
const char *other_text = "Serre";
#else
// English weekday and month names for date formatting on display
const char *days[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
const char *months[] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
const char *today_text = "Today";
const char *forecast_text = "Forecast";
const char *ha_sensor_text = "Home Assistant sensors";
const char *crypto_text = "Crypto";
const char *battery_text = "Battery";
const char *indoor_text = "Indoor";
const char *outdoor_text = "Outdoor";
const char *other_text = "Other";
#endif
