#ifndef SCREEN_H
#define SCREEN_H

// The game renders into an off-screen buffer of GAME_W x GAME_H, which is
// scaled down to the Vita's 960x544 display each frame. The game lays out its
// interface for this size, so menus get the room they have on a 720p screen
// (the station menu fits its system info above the buttons, like the PC
// version). Same 16:9 shape as the display.
#define GAME_W 1280
#define GAME_H 725

#define DISPLAY_W 960
#define DISPLAY_H 544

#endif
