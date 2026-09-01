/**
 * SFX - Non-blocking Sound Effects Module for Porkchop
 *
 * ==[ CHEF'S AUDIO ]== central beeps, no delay(), callback-safe enqueue.
 * 
 * CRITICAL: All sounds are non-blocking. Call SFX::update() from main loop.
 * Safe to call SFX::play() from anywhere including WiFi promiscuous callbacks.
 */

#ifndef SFX_H
#define SFX_H

#include <stdint.h>

namespace SFX {

// ===[ fR3K PERSONALITY VOICE WORDS ]==
// Demon vocab (v3). Five 2-step pitch contours + one blunt jingle.
// SOUND WORD settings row cycles ACK HEY NAH MUM OOF; CUNT JINGLE is the
// user-facing default per the operator's direct instruction.
enum VoiceWord : uint8_t {
    VOICE_ACK = 0,
    VOICE_HEY = 1,
    VOICE_NAH = 2,
    VOICE_MUM = 3,
    VOICE_OOF = 4,
    VOICE_CUNT = 5,
    VOICE_COUNT = 6,
};

// ==[ EVENTS ]== safe to call from anywhere
enum Event {
    NONE = 0,
    
    // === OINK MODE ===
    DEAUTH,             // deauth sent - low kick drum
    HANDSHAKE,          // complete handshake - victory arpeggio + morse GG
    PMKID,              // PMKID captured - quick double-tap
    NETWORK_NEW,        // new network found - soft tick
    
    // === SPECTRUM MODE ===
    CLIENT_FOUND,       // new client detected - high pip
    SIGNAL_LOST,        // signal lost - descending tones
    CHANNEL_LOCK,       // channel locked for monitoring
    REVEAL_START,       // client reveal mode started
    
    // === GAMIFICATION ===
    ACHIEVEMENT,        // achievement unlocked - fanfare
    LEVEL_UP,           // level up - ascending celebration
    JACKPOT_XP,         // 5x XP jackpot - rising arp
    ULTRA_STREAK,       // 20 capture streak - epic fanfare
    CHALLENGE_COMPLETE, // daily challenge done - rising tones
    CHALLENGE_SWEEP,    // all 3 challenges done - victory fanfare
    
    // === BLE SYNC ===
    CALL_RING,          // incoming call from Sirloin
    SYNC_COMPLETE,      // sync finished successfully
    
    // === SYSTEM ===
    ERROR,              // error buzz
    CLICK,              // UI click
    MENU_CLICK,         // menu navigation click
    TERMINAL_TICK,      // short terminal tick (boot variation)
    BOOT,               // device boot sequence
    PIGSYNC_BOOT,       // extended boot sequence for PIGSYNC
    
    // === SPECIAL ===
    SIREN,              // police siren effect (replaces flashSiren audio)
    YOU_DIED,           // Dark Souls style death sound

    // === UI FEEDBACK ===
    MODE_ENTER,         // mode transition in - quick ascending pair
    MODE_EXIT,          // mode transition out - quick descending pair
    CONFIRM,            // positive confirmation (settings saved)
    TYPING_KEY,         // ultra-short keystroke tick
    BACK_NAV,           // back/escape navigation

    // === PIG VOCALIZATIONS ===
    OINK_HAPPY,         // legacy alias kept for source compat (maps to ACK)
    OINK_GRUNT,         // legacy alias kept for source compat (maps to HEY)
    OINK_SQUEAL,        // legacy alias kept for source compat (maps to OOF)
    OINK_CURIOUS,       // legacy alias kept for source compat (maps to NAH)

    // === DEMON WORDS (v3) ===
    ACK,                // quick bright double-tap
    HEY,                // mid descending pair
    NAH,                // questioning upward
    MUM,                // soft murmur (low + warm)
    OOF,                // short falling thud
    CUNT,               // blunt descending jingle (~150 ms)

    // === AMBIENT SCANNING ===
    SONAR_PING,         // minimal single blip
    RADAR_SWEEP,        // subtle rising sweep
    SCAN_TICK,          // quiet periodic tick

    // === AMBIENT BIRDS ===
    BIRD_HIT,           // wave zaps bird - short electric zap
    BIRD_IMPACT,        // bird hits ground - low thud + crackle

    // === SCENE FX ===
    THUNDER,            // storm crack - low boom + sizzle
    WOLF,               // visitor growl / howl stub
    WOLF_HIT,           // pig smacks wolf — short yelp
    JUMP,               // pig hop - short rising boing
    ATTACK_HOP,         // pounce / stomp attack
    RAIN_TICK,          // quiet ambient drip (rain) — keep very soft
    IR_FIRE             // IR blaster charge / laser zap (play before mute TX)
};

// Initialize audio system (call once at startup)
void init();

// Queue a sound event (callback-safe, ring buffer)
// Safe to call from WiFi promiscuous callback, BLE callback, anywhere
void play(Event event);

// Pump audio from main loop - MUST be called regularly (~every 10-50ms)
// Returns true if still playing
bool update();

// Is anything currently playing?
bool isPlaying();

// Stop current playback and clear queue
void stop();

// Hard mute reasons (OR'd). play() silent while any reason is set.
// IR bitbang and G0 screen-off stack without clobbering each other.
static constexpr uint8_t MUTE_IR         = 1u << 0;
static constexpr uint8_t MUTE_SCREEN_OFF = 1u << 1;

// setMuted(true/false) toggles MUTE_IR (legacy IR paths)
void setMuted(bool muted);
// G0 brightness-0: silence everything until screen wakes
void setScreenOffMuted(bool muted);
// Any reason active?
bool isMuted();
// Current mute bit mask (debug / diagnostics)
uint8_t muteMask();

// Direct tone access (for special cases)
void tone(uint16_t freq, uint16_t duration);

// Play the personality's chosen demon word (SOUND WORD settings row).
// Picks ACK / HEY / NAH / MUM / OOF / CUNT from Config::personality().
// Called from Mood / boot splash / etc instead of the legacy OINK_* events.
// Honours the mute mask (when set, the personality event is skipped) but
// does NOT touch the audio queue or the active sequence, so a queued
// mood event that fires inside an IR blast window is preserved across
// the mute toggle.
void playPersonality();

// Short personality- aware UI nav tap (~50-60 ms total). Bypasses the
// audio queue and the sequence state machine - fires M5.Speaker.tone()
// directly so the screen update is never trailed by an audio tail.
// Honours soundLevel == 0 and the mute mask but does NOT clear the
// active sequence or drop queued events. Callers in tight loops (menu
// navigation, settings value cycling) should use this instead of
// SFX::play(SFX::CLICK).
void playNav();

// v3.0.2: per-screen audio gate. Called from setMode() in app.cpp.
// When true: play() / playPersonality() / playCuntJingle() / playNav()
// short-circuit to no-op. update() also no-ops so the sequence pump
// doesn't run. Use this to silence MENU and ATTACK root navigation
// without affecting FARM or any in-game mode.
void setMenuMode(bool inMenu);

// v3.0.2: when in menu mode AND Config::personality().menuMinimalTap
// is true, playNav() fires a 30 ms single piezo blip instead of being
// silent. Lets the operator keep audible feedback in the menu without
// paying the sequence-pump cost. Default off.
void setMinimalTap(bool enabled);

// One-shot fire of the CUNT jingle regardless of the configured word -
// used by settings UI / unlock confirmation when the operator wants
// to hear the jingle out of band.
void playCuntJingle();

}  // namespace SFX

#endif  // SFX_H
