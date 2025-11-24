#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include "address_map_niosv.h"

typedef uint16_t pixel_t;
typedef uint32_t device;
device prev_key = 0x0;

volatile pixel_t *pVGA = (pixel_t *)FPGA_PIXEL_BUF_BASE;

const pixel_t blk = 0x0000;
const pixel_t wht = 0xffff;
const pixel_t red = 0xf800;
const pixel_t grn = 0x07e0;
const pixel_t blu = 0x001f;

uint8_t hexDecoder(int score)
{
	if(score < 0)
		score = 0;
	if(score > 9)
		score = 9;

	uint8_t disp_val;

	switch(score)
	{
		case 0: 
			disp_val = 0xbf;
			break;
		case 1: 
			disp_val = 0x86;
			break;
		case 2: 
			disp_val = 0xdb;
			break;
		case 3: 
			disp_val = 0xcf;
			break;
		case 4: 
			disp_val = 0xe6;
			break;
		case 5: 
			disp_val = 0xed;
			break;
		case 6: 
			disp_val = 0xfd;
			break;
		case 7: 
			disp_val = 0x87;
			break;
		case 8: 
			disp_val = 0xff;
			break;
		case 9: 
			disp_val = 0xe7;
			break;
		default: 
			disp_val = 0x80;
			break;
	}

	disp_val &= 0x7f;           // I want to turn off the decimal point so i force the 8th bit OFF
	return disp_val;            // example: 1101_1011 & 0111_1111 = 0101_1011 (8th bit is OFF) 
}

void updateHex()
{

}

void delay( int N )
{
	for( int i=0; i<N; i++ ) 
		*pVGA; // read volatile memory location to waste time
}

void drawPixel( int y, int x, pixel_t colour )
{
	*(pVGA + (y<<YSHIFT) + x ) = colour;
}

pixel_t makePixel( uint8_t r8, uint8_t g8, uint8_t b8 )
{
	// inputs: 8b of each: red, green, blue
	const uint16_t r5 = (r8 & 0xf8)>>3; // keep 5b red
	const uint16_t g6 = (g8 & 0xfc)>>2; // keep 6b green
	const uint16_t b5 = (b8 & 0xf8)>>3; // keep 5b blue
	return (pixel_t)( (r5<<11) | (g6<<5) | b5 );
}

void rect( int y1, int y2, int x1, int x2, pixel_t c )
{
	for( int y=y1; y<y2; y++ )
		for( int x=x1; x<x2; x++ )
			drawPixel( y, x, c );
}

typedef struct player {
	int curr_x, curr_y;
	int dir_x, dir_y;
	bool alive; 
	pixel_t colour;
} player;

typedef struct game{
	player user;
	player bot;
	int userScore;
	int botScore;
	bool roundOver;
	bool gameOver;
} game;
	
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

void game_init(game *g)
{
	g -> userScore = 0;
	g -> botScore = 0;
	g -> roundOver = false;
	g -> gameOver = false;
	
	player_init(&g -> user, MAX_Y/2, MAX_X/3, 0, 1, blu);      // starting the user on the left side of the screen
	
	pixel_t yellow = makePixel(255, 255, 0);
	player_init(&g -> bot, MAX_Y/2, (2*MAX_X)/3, 0, -1, yellow);    // starting the bot on the right side of the screen
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
	volatile device *button = (device*)KEY_BASE;
	device curr_key = *button;
	// key0 CCW
	// (y,x) = (-y, x)
	// key1 CW
	// (y,x) = (y,-x)
	int prev_y;
	int prev_x;
	
	int prev0 = (prev_key & 0x1);
	int curr0 = (curr_key & 0x1);
	
	int prev1 = (prev_key & 0x2);
	int curr1 = (curr_key & 0x2);
	
	if((!prev0 && curr0) == 1)
	{
		prev_y = p -> dir_y;
		prev_x = p -> dir_x;
		
		p -> dir_y = -prev_x;
		p -> dir_x = prev_y;
	}
	
	if((!prev1 && curr1) == 1)
	{
		prev_y = p -> dir_y;
		prev_x = p -> dir_x;
		
		p -> dir_y = prev_x;
		p -> dir_x = -prev_y;
	}
	
	prev_key = curr_key;
}
void resetRound(game *g)
{
	rect(5, MAX_Y - 5, 5, MAX_X - 5, blk);

	player_init(&g -> user, MAX_Y/2, MAX_X/3, 0, 1, blu);
	pixel_t yellow = makePixel(255,255,0);
	player_init(&g -> bot, MAX_Y/2, 2*(MAX_X)/3, 0, -1, yellow);

	drawPixel(g -> user.curr_y, g -> user.curr_x, g -> user.colour);
	drawPixel(g -> bot.curr_y, g -> bot.curr_x, g -> bot.colour);

	g -> roundOver = false;
}
int main()
{	
	volatile uint32_t *hex0 = (uint32_t*)HEX3_HEX0_BASE;
	*hex0 = hexDecoder(2);
	
	rect(0, MAX_Y, 0, MAX_X, wht);     
	rect(5, MAX_Y - 5, 5, MAX_X - 5, blk );  
	
	game tronGame;                         // create an instance of game "tronGame"
	game_init(&tronGame);
	
	drawPixel(tronGame.user.curr_y, tronGame.user.curr_x, tronGame.user.colour);
	drawPixel(tronGame.bot.curr_y, tronGame.bot.curr_x, tronGame.bot.colour);
	
	while(tronGame.gameOver != true)
	{
		updatePlayer(&tronGame.user);
		updateBot(&tronGame.bot);
		
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
			
		    	if(tronGame.userScore >= 9 || tronGame.botScore >= 9)
					tronGame.gameOver = true;
		    	else
					resetRound(&tronGame);
			}	
		delay(1000000);
	}
	
	return 0;
}