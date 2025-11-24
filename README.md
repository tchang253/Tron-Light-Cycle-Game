# Tron Light-Cycle Game (FPGA - Embedded C)
A personal project exploring embedded C, bare-metal programming and real-time graphics on the Intel/Altera DE10-Lite FPGA.
The game is a simple Tron-style light-cycle arena where the player competes against a bot, with all rendering done directly to the VGA framebuffer.

## Project Goal
This project was built to explore:
* Writing embedded C for a Nios V soft-core CPU
* Memory-mapped I/O on FPGA hardware
* Input handling via physical pushbuttons
* Implementing simple deterministic game AI
* bare-metal timing and game-loop structure

## Display System (VGA Framebuffer)
The DE10-Lite FPGA provides a 160x120 RGB565 framebuffer mapped to SDRAM.
Pixels are written directly using pointers:
```
*(pVGA + (y << YSHIFT) + x) = colour;
```
* `YSHIFT = 8` -> each row is 256 bytes
* Rendering requires no OS or graphics binary
* The framebuffer itself acts as the game board (every trail pixel stays on the screen and becomes a collision)
  
Colours are generated using 16-bit RGB565 and the rectangles/borders are drawn with simple loops.

## Player Controls
The user rotates the cycle using the pushbuttons on the DE10-lite:
* KEY0 -> rotate left (CCW)
* KEY1 -> rotate right (CW)

Direction is stored as a vector `(dir_x, dir_y)` and rotated using 2D transformations:
* CCW: `(x, y) -> (-y, x)`
* CW: `(x, y) -> (y, -x)`

The code detects rising edges of button inputs so each press registers only once, producing clean input without requiring debouncing hardware. 

## Bot Control (Directional Look-Ahead) 
The bot uses a simple but effective look-ahead strategy:
1. Simulated movement
   The bot evaluates three possible directions (straight, left, right) using a look-ahead function:
   ```
   bool botCollision(player *p, int dy, int dx, int steps);
   ```
   This checks:
   * Borders (with a 5-pixel margin)
   * trail pixels (non-black framebuffer values)

2. Direction selection
   During every frame, the bot tries possible directions in this priority:
   1. Go straight (if safe)
   2. else turn left
   3. else turn right

   If all three paths are bad, the bot is trapped and will eventually collide.
   This produces primitive behaviour: the bot avoids walls, avoids both its own trail and the user's trail and prefers open spaces.

## Collision Detection
Before a player moves, the game checks the pixel one step ahead:
```
pixel_t colour = readPixel(next_y, next_x);
```
A collision occurs if:
* The next position results in hitting the border
* The next position is non-black (a trail)
* The next position is outside the playable area

the trails are permanent (until the round is finished), so the VGA buffer becomes the collision grid.

### Game Rules
* If one crashes, the other scores a point
* First to 9 points wins the match
* The arena resets between rounds
* A simple busy-wait delay sets game speed

### Building & Running
Requires Quartus Prime Lite and Nios V tools. 
```
make DE10-Lite
make GDB_SERVER
make TERMINAL
make COMPILE
make RUN
```
Connect the DE10-Lite via USB-Blaster and VGA output.
