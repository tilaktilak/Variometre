# variometre
Projet variometre de parapente sur Arduino Pro Mini

Le but du projet est de faire un variomètre de parapente avec les fonctions suivantes : 
  - Mesure de la température, de l'altitude
  - Bip correspondant à un ascendance/descendance
  - Mesure de la position GPS
  - Log des données sur carte SD au format IGC
  - Affichage sur écran OLED
  - Alimentation par LiPo 1s, avec chargeur USB

Cette branche prend pour base la librairie https://github.com/greiman/NilRTOS-Arduino
notament le code exemple SD logger. 

On peut alors threader le programme : 

  Un thread de lecture des capteurs     (periode >=500ms)
  Un thread d'écriture sur la carte SD  (pas périodique)
  Si possible un thread de gestion GUI avec l'écran et les inters (periode lente)
  Communication par FIFO entre thread capteur et thread SD.
  
