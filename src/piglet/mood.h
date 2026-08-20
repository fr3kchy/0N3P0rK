// Tamagotchi mood - care for the pig. No radio / capture theater.
#pragma once

#include <M5Unified.h>
#include "avatar.h"

class Mood {
public:
    static void init();
    static void update();
    static void draw(M5Canvas& canvas);
    static void saveMood();

    static void feed();
    static void eatWorld();      // fruit/berry she found herself
    static void onZombieApplied();  // curse: 5 hearts → empty, eat fruit to heal
    static void hurt(int amount);
    static void pet();
    static void play();          // walk / jump
    static void onBirdKill();    // scene toy, not radio
    static void onIdle();

    static void adjustHappiness(int delta);
    static int getCurrentHappiness();
    static int getEffectiveHappiness();
    static int getLastEffectiveHappiness();
    static int getHunger();   // 0–100 food percent
    static int getLife();     // 0–100 mapped from hearts
    static int getHearts();   // 0–5 discrete hearts
    static uint32_t getLastActivityTime();
    static const char* getCurrentPhrase();

    static void setStatusMessage(const char* msg);
    static void setDialogueLock(bool) {}
    static bool isDialogueLocked() { return false; }

private:
    static int happiness;
    static int hunger;
    static int life;
    static char currentPhrase[40];
    static uint32_t lastPhraseChange;
    static uint32_t lastActivityTime;
    static uint32_t lastDecayMs;
    static int lastEffective;

    static void pickPhrase();
    static void updateAvatarState();
    static void say(const char* phrase);
    static int addFood(int amount);
    static void maybeCureZombie();
};
