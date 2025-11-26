#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include "address_map_niosv.h"

#define CLOCK_SPEED 50000000

#define GLOBAL_MIE 0x8
#define MTIME_MIE 0x80
#define EXTERNAL_MIE 0x00040000

// mtime_ptr + 0 -> mtime_lo    -> lower 32 bits of mtime counter      (MTIMER_BASE + 0)
// mtime_ptr + 1 -> mtime_hi    -> upper 32 bits of mtime counter      (MTIMER_BASE + 4)
// mtime_ptr + 2 -> mtimecmp_lo -> lower 32 bits of mtimecmp value     (MTIMER_BASE + 8)
// mtime_ptr + 3 -> mtimecmp_hi -> upper 32 bits of mtimecmp value     (MTIMER_BASE + 12)

// key_ptr + 0 -> data register                   (KEY_BASE + 0)
// key_ptr + 1 -> direction register              (KEY_BASE + 4)
// key_ptr + 2 -> enable specific key interrupts  (KEY_BASE + 8)
// key_ptr + 3 -> clear edges                     (KEY_BASE + 12)

volatile int flag = 0;
volatile int key_flag = 0;
volatile uint64_t period = 0;

volatile uint32_t *mtime_ptr = (uint32_t*)MTIMER_BASE;
volatile uint32_t *ledr_ptr = (uint32_t*)LEDR_BASE;         
volatile uint32_t *key_ptr = (uint32_t*)KEY_BASE;      
volatile uint32_t *sw_ptr = (uint32_t*)SW_BASE;     

void update_gamespeed()
{
	int fps;

	uint32_t sw = *sw_ptr;
	sw &= 0xf; 

	if(sw == 0) 
		fps = 1; 
	
	else if(sw == 1)
		fps = 5;
	
	else if(sw == 2)
		fps = 10;
	
	else if(sw == 3)
		fps = 15;
	
	else
		fps = 15;

	period = CLOCK_SPEED / fps;
}

uint64_t read_mtime(volatile uint32_t *time_ptr)
{
	uint32_t mtime_hi;
	uint32_t mtime_lo;
	uint64_t mtime;

	do 
	{
		mtime_hi = *(time_ptr + 1);
		mtime_lo = *(time_ptr);
	} while(mtime_hi != *(time_ptr + 1));

	return mtime = ((uint64_t)mtime_hi << 32) | mtime_lo;
}

void set_mtime(volatile uint32_t *time_ptr, uint64_t mtime)
{
	*(time_ptr) = 0;            // temporarily set the mtime_lo to 0 to prevent potential roll-over during reading. 

	*(time_ptr + 1) = (uint32_t)(mtime >> 32);         // set mtime_hi by shifting 32 bits down then casting as a 32 bit number

	*(time_ptr) = (uint32_t)mtime;                     // set mtime_lo 
}

void setup_mtimecmp()
{
	uint64_t mtime_now, mtime_next;

	mtime_now = read_mtime(mtime_ptr);
	mtime_next = mtime_now + period;

	set_mtime(mtime_ptr + 2, mtime_next);              // writes both mtimecmp_hi & mtimecmp_lo into the registers 
}

void mtime_ISR()
{
	uint64_t mtimecmp;
	mtimecmp = read_mtime(mtime_ptr + 2);       // reading the current mtimecmp_hi & mtimecmp_lo values into mtimecmp variable

	mtimecmp += period;                   // next mtimecmp value = one period later in the future

	set_mtime(mtime_ptr + 2, mtimecmp);       // set the next mtimecmp value

	//*ledr_ptr ^= 0x1;

	flag = 1;
}

void setup_key_ISR()
{
	volatile uint32_t *key_dir = key_ptr + 1;
	volatile uint32_t *key_mask = key_ptr + 2;
	volatile uint32_t *key_edge = key_ptr + 3;

	*key_dir = 0x0;    // 0 indicates input 
	*key_mask = 0x3;
	*key_edge = 0xff;
}
void key_ISR()
{
	uint32_t key_val = *(key_ptr + 3);                       // reading from edge capture register only 

	int key0 = key_val & 0x1;
	int key1 = key_val & 0x2;

	if(key0 && !key1)
	{	
		if(key_flag == -1)
		{
			key_flag = 0;
			*ledr_ptr &= ~0x1;
		}
		else
		{
			key_flag = -1;
			*ledr_ptr |= 0x1;
		}
	}

	else if(!key0 && key1)
	{
		if(key_flag == 1)
		{
			key_flag = 0;
			*ledr_ptr &= ~0x2;
		}
		else
		{
			key_flag = 1;
			*ledr_ptr |= 0x2;
		}
	}

	else if((key0 && key1) || (!key0 && !key1))
	{
		key_flag = 0;
		*ledr_ptr &= ~0x3;
	}

	*(key_ptr + 3) = key_val;                              // writing back to edge capture register to clear the edges (read/write 1 to clear) wtf? 
}

void handler(void) __attribute__ ((interrupt("machine")));

void handler(void)
{
	uint32_t mcause_val;

	__asm__ volatile("csrr %0, mcause" : "=r"(mcause_val));         // reads the mcause_val into 

	if(mcause_val == 0x80000007)
		mtime_ISR();

	else if(mcause_val == 0x80000012)
		key_ISR();
}

void cpu_irq(uint32_t mask)
{
	__asm__ volatile("csrw mtvec, %0" :: "r"(handler));

	__asm__ volatile("csrs mie, %0" :: "r"(mask));

	__asm__ volatile("csrs mstatus, %0" :: "r"(GLOBAL_MIE));
}

typedef uint16_t pixel_t;
typedef uint32_t device;
device prev_key = 0x0;

volatile pixel_t *pVGA = (pixel_t *)FPGA_PIXEL_BUF_BASE;

const pixel_t blk = 0x0000;
const pixel_t wht = 0xffff;
const pixel_t red = 0xf800;
const pixel_t grn = 0x07e0;
const pixel_t blu = 0x001f;
const pixel_t mgt = 0xf81f;
const pixel_t ylw = 0xffe0;

pixel_t makePixel( uint8_t r8, uint8_t g8, uint8_t b8 )
{
	// inputs: 8b of each: red, green, blue
	const uint16_t r5 = (r8 & 0xf8)>>3; // keep 5b red
	const uint16_t g6 = (g8 & 0xfc)>>2; // keep 6b green
	const uint16_t b5 = (b8 & 0xf8)>>3; // keep 5b blue
	return (pixel_t)( (r5<<11) | (g6<<5) | b5 );
}

typedef struct player {
	int curr_x, curr_y;
	int dir_x, dir_y;
	bool alive; 
	pixel_t colour;
} player;

typedef struct game {
	player user;
	player bot;
	int userScore;
	int botScore;
	bool roundOver;
	bool gameOver;
} game;

typedef struct obstacle_t {
	int y1, y2;
	int x1, x2;
	pixel_t colour;
} obstacle_t;

#define NUM_OBS 3
obstacle_t obstacles[NUM_OBS] = {
	{30, 50, 50, 60, wht},
	{70, 80, 20, 80, wht},
	{40, 60, 100, 110, wht}
};

uint8_t hexDecoder(int score)
{
	if(score < 0)
		score = 0;
	if(score > 9)
		score = 9;

	switch(score)
	{
		case 0: return 0x3f;
		case 1: return 0x06;
		case 2: return 0x5b;
		case 3: return 0x4f;
		case 4: return 0x66;
		case 5: return 0x6d;
		case 6: return 0x7d;
		case 7: return 0x07;
		case 8: return 0x7f;
		case 9: return 0x67;
		default: return 0x00;
	}

	// disp_val &= 0x7f;           // I want to turn off the decimal point so i force the 8th bit OFF
	// return disp_val;            // example: 1101_1011 & 0111_1111 = 0101_1011 (8th bit is OFF) 
}

void updateHex(const game *g)         // only READING not modifying the game struct
{
	volatile uint32_t *hex = (uint32_t*)HEX3_HEX0_BASE;
	uint8_t bot_hex = hexDecoder(g -> botScore);
	uint8_t user_hex = hexDecoder(g -> userScore);

	uint32_t hex_value = 0;

	hex_value |= bot_hex;
	hex_value |= ((uint32_t)user_hex << 16);

	*hex = hex_value;
}

void drawPixel( int y, int x, pixel_t colour )
{
	*(pVGA + (y<<YSHIFT) + x ) = colour;
}

void rect( int y1, int y2, int x1, int x2, pixel_t c )
{
	for( int y=y1; y<y2; y++ )
		for( int x=x1; x<x2; x++ )
			drawPixel( y, x, c );
}

pixel_t readPixel(int y, int x)
{
	return *(pVGA +(y << YSHIFT) + x);
}

bool collisionDetection(player *p)              // pass by pointer NOT pass by reference
{
	int next_y = (p -> curr_y) + (p -> dir_y);      // looking one step ahead 
	int next_x = (p -> curr_x) + (p -> dir_x);
	
	if(next_x > MAX_X - 5 || next_y > MAX_Y - 5 || next_x < 5 || next_y < 5)         // bounds detection
	{
		p -> alive = false;  
		return true;                                          // out of bounds detected                    
	}
	
	pixel_t colour = readPixel(next_y, next_x);

	if(colour != blk)                                 // collision should happen hitting any colour thats not black
	{
		p -> alive = false;                           // directly editing the struct
		return true;                                  // collision DID happen
	}
	
	else
		return false;                                 // collision or out of bounds DID NOT happen
}

void player_init(player *p, int start_y, int start_x, int dir_y, int dir_x, pixel_t colour)
{
	p -> curr_y = start_y;
	p -> curr_x = start_x;
	p -> dir_y = dir_y;
	p -> dir_x = dir_x;
	p -> colour = colour;
	p -> alive = true;
}
void drawObstacles()
{
	for(int i = 0; i < NUM_OBS; i++)
	{
		rect(obstacles[i].y1, obstacles[i].y2, obstacles[i].x1, obstacles[i].x2, obstacles[i].colour);
	}
}

void game_init(game *g)
{
	g -> userScore = 0;
	g -> botScore = 0;
	g -> roundOver = false;
	g -> gameOver = false;

	drawObstacles();

	player_init(&g -> user, MAX_Y/2, MAX_X/3, 0, 1, blu);      // starting the user on the left side of the screen
	player_init(&g -> bot, MAX_Y/2, (2*MAX_X)/3, 0, -1, ylw);    // starting the bot on the right side of the screen
}


void movePlayer(player *p)
{
	drawPixel(p -> curr_y, p -> curr_x, p -> colour);
	
	p -> curr_y = p -> curr_y + p -> dir_y;
	p -> curr_x = p -> curr_x + p -> dir_x;
	
	drawPixel(p -> curr_y, p -> curr_x, p -> colour);
}

bool botCollision(player *p, int test_y, int test_x, int steps)
{
	int next_y = p -> curr_y;
	int next_x = p -> curr_x;

	for(int i = 0; i < steps; i++)
	{
		next_y += test_y;
		next_x += test_x;

		if(next_y > MAX_Y - 5 || next_x > MAX_X - 5 || next_y < 5 || next_x < 5)
			return true;

		pixel_t colour = readPixel(next_y, next_x);
		if(colour != blk)
			return true;
	}
	return false;
}

void updateBot(player *p)
{
	int dy = p -> dir_y;
	int dx = p -> dir_x;

	int ldy = dx;
	int ldx = -dy;

	int rdy = -dx;
	int rdx = dy;

	int look_ahead = 5;

	if(botCollision(p, dy, dx, look_ahead) == false)       // case 1: just go straight
	{
		p -> dir_y = dy;
		p -> dir_x = dx;
	}

	else if(botCollision(p, ldy, ldx, look_ahead) == false)        // case 2: go left 
	{
		p -> dir_y = ldy;
		p -> dir_x = ldx;
	}

	else if(botCollision(p, rdy, rdx, look_ahead) == false)        // case 3: go right
	{
		p -> dir_y = rdy;
		p -> dir_x = rdx;
	}
	                                                         // case 4: no safe options bot will just collide
}

void updatePlayer(player *p)
{
	int prev_y = p -> dir_y;
	int prev_x = p -> dir_x;

	if(key_flag == -1)
	{
		p -> dir_y = -prev_x;
		p -> dir_x = prev_y;
	}
	
	if(key_flag == 1)
	{
		p -> dir_y = prev_x;
		p -> dir_x = -prev_y;
	}

	*ledr_ptr = 0x0;
	key_flag = 0;
}

void resetRound(game *g)
{
	rect(5, MAX_Y - 5, 5, MAX_X - 5, blk);
	drawObstacles();

	player_init(&g -> user, MAX_Y/2, MAX_X/3, 0, 1, blu);
	player_init(&g -> bot, MAX_Y/2, 2*(MAX_X)/3, 0, -1, ylw);

	drawPixel(g -> user.curr_y, g -> user.curr_x, g -> user.colour);
	drawPixel(g -> bot.curr_y, g -> bot.curr_x, g -> bot.colour);

	g -> roundOver = false;
}

void winScreen(const game *g)
{
	pixel_t win_colour;

	if(g -> userScore > g -> botScore)
		win_colour = blu;
	
	if(g -> userScore < g -> botScore)
		win_colour = ylw;

	rect(0, MAX_Y, 0, MAX_X, win_colour);
}


int main()
{	
	update_gamespeed();
	setup_mtimecmp();
	setup_key_ISR();

	cpu_irq(MTIME_MIE | EXTERNAL_MIE);

	rect(0, MAX_Y, 0, MAX_X, wht);     
	rect(5, MAX_Y - 5, 5, MAX_X - 5, blk );  
	
	game tronGame;                         // create an instance of game "tronGame"
	game_init(&tronGame);
	updateHex(&tronGame);
	
	drawPixel(tronGame.user.curr_y, tronGame.user.curr_x, tronGame.user.colour);
	drawPixel(tronGame.bot.curr_y, tronGame.bot.curr_x, tronGame.bot.colour);
	
	while(tronGame.gameOver != true)
	{
		if(flag == 1)
		{
			flag = 0;
			update_gamespeed();
			updatePlayer(&tronGame.user);
		    updateBot(&tronGame.bot);

			uint32_t pause = *sw_ptr;
			if(pause & (1<<9))
				continue;
		
			if((tronGame.user.alive == true) && (collisionDetection(&tronGame.user) == false))
				movePlayer(&tronGame.user);
		
			if((tronGame.bot.alive == true) && (collisionDetection(&tronGame.bot) == false))
				movePlayer(&tronGame.bot);
		
			if((tronGame.user.alive == false) || (tronGame.bot.alive == false))
				{
					tronGame.roundOver = true;

					if(tronGame.user.alive == false && tronGame.bot.alive == true)
						tronGame.botScore ++;

					else if(tronGame.user.alive == true && tronGame.bot.alive == false)
						tronGame.userScore ++;

					updateHex(&tronGame);
			
		    		if(tronGame.userScore >= 9 || tronGame.botScore >= 9)
						tronGame.gameOver = true;
		    		else
						resetRound(&tronGame);
				}	
		}
	}
	winScreen(&tronGame);
	return 0;
}