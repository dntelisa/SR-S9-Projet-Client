# Client de jeu multijoueur

Ce projet est le client graphique d'un jeu multijoueur type Pacman. Écrit en **C++**, il se connecte au serveur Go via **WebSockets**, affiche l'état du jeu avec fluidité grâce à l'interpolation, et gère les entrées utilisateur. Ce client a été conçu pour fonctionner avec le serveur: https://github.com/dntelisa/SR-S9-Projet-Serveur

## Fonctionnalités 

* **Rendu graphique :** Utilisation de **Raylib** pour un affichage performant et léger.
* **Architecture multithreadée :** Séparation stricte entre le thread principal (UI) et le thread réseau (réception des messages WebSocket).
* **Synchronisation (thread-safety) :** Utilisation de `std::mutex` pour protéger les données partagées (positions des joueurs) et éviter les accès concurrents.
* **Interpolation de mouvement (lag compensation) :** Le client calcule une transition fluide entre deux états du serveur pour masquer le taux de rafraîchissement du serveur (20Hz) et afficher une animation à 60 FPS.
* **Gestion réseau :** Reconnexion automatique en cas de perte de connexion.
* **Mode "Headless" :** Un mode sans interface graphique pour lancer des bots et effectuer des tests de charge sur le serveur.

## Dépendances

Le projet utilise les bibliothèques suivantes :
* **Raylib** : Moteur graphique.
* **IXWebSocket** : Client WebSocket C++.
* **nlohmann/json** : Manipulation du format JSON.
* **CMake** : Système de build.

## Compilation et Installation

Assurez-vous d'avoir `cmake` et un compilateur C++ installés.

1.  **Créer le dossier de build :**
    ```bash
    mkdir build
    cd build
    ```

2.  **Configurer et Compiler :**
    ```bash
    cmake ..
    make
    ```

3.  **Lancer le client :**
    ```bash
    ./srclient
    ```

## Utilisation

Le client peut être configuré via des arguments en ligne de commande :

### Lancement standard
Se connecte à `localhost:8080/ws` avec le nom "cpp-player".
```bash
./srclient

```

### Options personnalisées

* `--server=` : Adresse du serveur (ex: connexion à une VM).
* `--name=` : Choisir son pseudo.
* `--headless` : Lance le client sans fenêtre (mode bot).

**Exemple :**

```bash
./srclient --server=ws://192.168.1.50:80/ws --name=Elisa

```

### Mode test

Pour tester la robustesse du serveur ou jouer contre des bots, vous pouvez lancer plusieurs clients sans interface graphique qui se déplacent aléatoirement :

```bash
./srclient --headless --name=Bot1

```

## Contrôles

* **Flèches Directionnelles (Haut, Bas, Gauche, Droite) :** Déplacer le personnage.
* **Bouton "Reconnect" (Interface) :** Force une reconnexion immédiate au serveur.

## Architecture Technique

### 1. Boucle principale (`main`)

La boucle `while (!WindowShouldClose())` tourne à **60 FPS**.

* **Input :** Détecte les touches et envoie les commandes `move` au serveur.
* **Interpolation :** Calcule la position visuelle `(x, y)` en fonction du temps écoulé depuis le dernier paquet serveur (`t`), assurant un mouvement fluide.
```cpp
pos = oldPos + (targetPos - oldPos) * t

```


* **Rendu :** Dessine la grille, les bonbons et les joueurs avec Raylib.

### 2. Gestion réseau (callback asynchrone)

Le réseau est géré par `ix::WebSocket` dans un thread séparé.

* **Réception (`onMessage`) :** Parse le JSON entrant (`state`, `join_ack`, `game_over`).
* **Verrouillage :** Utilise `std::lock_guard<std::mutex>` avant de mettre à jour les structures de données (`players`, `sweets`) pour éviter que le thread de rendu ne lise des données en cours de modification.

---

**Auteur :** Elisa Donet - Eliott Houcke
**Projet :** Système Répartis - ESIR
