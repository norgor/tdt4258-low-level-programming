#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#include <linux/input.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <poll.h>

#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <linux/input.h>


// The game state can be used to detect what happens on the playfield
#define GAMEOVER   0
#define ACTIVE     (1 << 0)
#define ROW_CLEAR  (1 << 1)
#define TILE_ADDED (1 << 2)

// color stores a 24-bit color value with 8 bits for R, G and B.
typedef struct {
	char r, g, b;
} color;

// If you extend this structure, either avoid pointers or adjust
// the game logic allocate/deallocate and reset the memory
typedef struct {
	bool occupied;
	color color;
} tile;

typedef struct {
	unsigned int x;
	unsigned int y;
} coord;

typedef struct {
	coord const grid;                     // playfield bounds
	unsigned long const uSecTickTime;     // tick rate
	unsigned long const rowsPerLevel;     // speed up after clearing rows
	unsigned long const initNextGameTick; // initial value of nextGameTick

	unsigned int tiles; // number of tiles played
	unsigned int rows;  // number of rows cleared
	unsigned int score; // game score
	unsigned int level; // game level

	tile *rawPlayfield; // pointer to raw memory of the playfield
	tile **playfield;   // This is the play field array
	unsigned int state;
	coord activeTile;                       // current tile

	unsigned long tick;         // incremeted at tickrate, wraps at nextGameTick
															// when reached 0, next game state calculated
	unsigned long nextGameTick; // sets when tick is wrapping back to zero
															// lowers with increasing level, never reaches 0
} gameConfig;



gameConfig game = {
									 .grid = {8, 8},
									 .uSecTickTime = 10000,
									 .rowsPerLevel = 2,
									 .initNextGameTick = 50,
};

// Sense Hat Framebuffer
#define SENSE_HAT_FB_ID ("RPi-Sense FB")
#define SENSE_HAT_FB_WIDTH (8)
#define SENSE_HAT_FB_HEIGHT (8)
#define SENSE_HAT_FB_SIZE (SENSE_HAT_FB_WIDTH*SENSE_HAT_FB_HEIGHT*sizeof(senseHatPixel))

#define SENSE_HAT_FB_R_BITS 5
#define SENSE_HAT_FB_G_BITS 6
#define SENSE_HAT_FB_B_BITS 5
// senseHatPixel represents a pixel of the sense hat.
// the struct is packed and aligned to 1 in order to be able to address the
// color values correctly.
typedef struct senseHatPixel {
	// each of these values has SENSE_HAT_FB_X_BITS bits.
	char b : SENSE_HAT_FB_B_BITS;
	char g : SENSE_HAT_FB_G_BITS;
	char r : SENSE_HAT_FB_R_BITS;
} __attribute__((aligned(1), packed)) senseHatPixel;
senseHatPixel *senseHatFb;

// Sense Hat Joystick
#define SENSE_HAT_JOYSTICK_NAME ("Raspberry Pi Sense HAT Joystick")
int senseHatJoystickFd = -1;


// Color picker
#define CPICKER_COLS_LEN (sizeof(cpicker_cols)/sizeof(color))
const color cpicker_cols[] = {
	{ .r=255, .g=000, .b=000 },
	{ .r=255, .g=255, .b=000 },
	{ .r=000, .g=255, .b=000},
	{ .r=000, .g=255, .b=255 },
	{ .r=000, .g=000, .b=255},
	{ .r=255, .g=000, .b=255 },
};
int cpicker_idx = 0;

// cpicker picks a color from the cpicker colors in a circular fashion.
color cpicker() {
	color c = cpicker_cols[cpicker_idx];
	cpicker_idx = (cpicker_idx + 1) % CPICKER_COLS_LEN;
	return c;
}

// colorToPixel converts a color to a pixel value.
senseHatPixel colorToPixel(const color c) {
	// max values for each of the colors
	// 1 << a is equal to 2^a
	// e.g. (1 << 8)-1 is 255
	int rmax = (1 << SENSE_HAT_FB_R_BITS)-1;
	int gmax = (1 << SENSE_HAT_FB_G_BITS)-1;
	int bmax = (1 << SENSE_HAT_FB_B_BITS)-1;
	return (senseHatPixel){
		.r = (c.r*rmax)/255,
		.g = (c.g*gmax)/255,
		.b = (c.b*bmax)/255,
	};
}

bool initializeSenseHatFb() {
	// max fb is fb31, this will ensure enough data is allocated on the stack
	char fb[] = "/dev/fb31";
	int fd = -1;
	bool ok = false;
	struct fb_fix_screeninfo info;
	
	// Loop until the sense hat fb is found
	for (int i = 0; !ok && i < 32; i++) {
		if (fd >= 0) close(fd);

		sprintf(fb, "/dev/fb%d", i);
		fd = open(fb, O_NONBLOCK | O_RDWR);
		if (fd < 0) {
			// exit if we have reached the end
			if (errno == ENOENT) break;

			perror("unable to open framebuffer");
			continue;
		}
		// get info about the framebuffer
		if (ioctl(fd, FBIOGET_FSCREENINFO, &info) == -1) {
			perror("unable to get fixed screen info");
			continue;
		}
		// compare ID to check if this is the correct framebuffer
		if (strcmp(info.id, SENSE_HAT_FB_ID)) continue;
		
		// map the framebuffer into memory
		void* map = mmap(NULL, SENSE_HAT_FB_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		if (map == MAP_FAILED) {
			perror("unable to memory-map the framebuffer");
			break;
		}
		// the fd is not needed after mmaping
		close(fd);

		ok = true;
		senseHatFb = map;
	}

	return ok;
}

bool initializeSenseHatJoystick() {
	// max ev is event31, this will ensure enough data is allocated on the stack
	char ev[] = "/dev/input/event31";
	// name only needs to store the name of the sense hat joystick 
	char name[sizeof(SENSE_HAT_JOYSTICK_NAME)];
	int fd = -1;
	bool ok = false;

	// Loop until the sense hat joystick is found
	for (int i = 0; !ok && i < 32; i++) {
		if (fd >= 0) close(fd);
		
		sprintf(ev, "/dev/input/event%d", i);
		fd = open(ev, O_NONBLOCK | O_RDWR);
		if (fd < 0) {
			// exit if we have reached the end
			if (errno == ENOENT) break;

			perror("unable to open input device");
			continue;
		}
		// get info about the input event
		if (ioctl(fd, EVIOCGNAME(sizeof(SENSE_HAT_JOYSTICK_NAME)-1), name) == -1) {
			perror("unable to get input device name");
			continue;
		}
		// compare name to check if this is the correct input event
		if (strcmp(name, SENSE_HAT_JOYSTICK_NAME)) continue;
		
		// set the global joystick fd variable
		senseHatJoystickFd = fd;
		ok = true;
	}

	if (!ok && fd >= 0) close(fd);

	return ok;
}

// This function is called on the start of your application
// Here you can initialize what ever you need for your task
// return false if something fails, else true
bool initializeSenseHat() {
	// initialization is only succcessful if we can get both the fb and joystick.
	return (
		initializeSenseHatFb() && 
		initializeSenseHatJoystick()
	);
}

// This function is called when the application exits
// Here you can free up everything that you might have opened/allocated
void freeSenseHat() {
	munmap(senseHatFb, SENSE_HAT_FB_SIZE);
	close(senseHatJoystickFd);
}

// Polls the sense hat joystick for inputs.
// Returns true if inputs are available to read, else false.
bool pollSenseHatJoystick() {
	struct pollfd pfd = {
		// we want to read from the sense hat joystick fd
		.fd = senseHatJoystickFd,
		// POLLIN will cause poll to return when we can read more data
		.events = POLLIN,
		.revents = 0,
	};
	// poll without timeout so that we do not block the loop
	int pres = poll(&pfd, 1, 0);
	if (pres == -1) {
		perror("unable to poll joystick");
		return false;
	}
	return pres != 0;
}

// This function should return the key that corresponds to the joystick press
// KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, with the respective direction
// and KEY_ENTER, when the the joystick is pressed
// !!! when nothing was pressed you MUST return 0 !!!
int readSenseHatJoystick() {
	int key = 0;
	struct input_event ie;

	// Read until a key is found or there is nothing left to read.
	while (key == 0 && pollSenseHatJoystick()) {
		// Read the event
		if (read(senseHatJoystickFd, &ie, sizeof(ie)) == -1) {
			perror("unable to read joystick input");
			return 0;
		}
		
		// must be a pressed / autorepeat event
		if (ie.type != EV_KEY) continue;
		if (ie.value == 0) continue;

		key = ie.code;
	} 

	return key;
}


// This function should render the gamefield on the LED matrix. It is called
// every game tick. The parameter playfieldChanged signals whether the game logic
// has changed the playfield
void renderSenseHatMatrix(bool const playfieldChanged) {
	// exit if no changes in the playfield
	if (!playfieldChanged) return;

	// loop over every tile and update its corresponding pixel
	for (int x = 0; x < game.grid.x; x++) {
		for (int y = 0; y < game.grid.y; y++) {
			// find the tile and convert its color to pixel. an unoccupied tile
			// is black, so we dont need any explicit checking.
			tile t = game.playfield[x][y];
			senseHatFb[x * SENSE_HAT_FB_WIDTH + y] = colorToPixel(t.color);
		}
	}
}


// The game logic uses only the following functions to interact with the playfield.
// if you choose to change the playfield or the tile structure, you might need to
// adjust this game logic <> playfield interface

static inline void newTile(coord const target) {
	tile *t = &game.playfield[target.y][target.x];
	t->occupied = true;
	// pick a color from the cpicker!
	t->color = cpicker();
}

static inline void copyTile(coord const to, coord const from) {
	memcpy((void *) &game.playfield[to.y][to.x], (void *) &game.playfield[from.y][from.x], sizeof(tile));
}

static inline void copyRow(unsigned int const to, unsigned int const from) {
	memcpy((void *) &game.playfield[to][0], (void *) &game.playfield[from][0], sizeof(tile) * game.grid.x);

}

static inline void resetTile(coord const target) {
	memset((void *) &game.playfield[target.y][target.x], 0, sizeof(tile));
}

static inline void resetRow(unsigned int const target) {
	memset((void *) &game.playfield[target][0], 0, sizeof(tile) * game.grid.x);
}

static inline bool tileOccupied(coord const target) {
	return game.playfield[target.y][target.x].occupied;
}

static inline bool rowOccupied(unsigned int const target) {
	for (unsigned int x = 0; x < game.grid.x; x++) {
		coord const checkTile = {x, target};
		if (!tileOccupied(checkTile)) {
			return false;
		}
	}
	return true;
}


static inline void resetPlayfield() {
	for (unsigned int y = 0; y < game.grid.y; y++) {
		resetRow(y);
	}
}

// Below here comes the game logic. Keep in mind: You are not allowed to change how the game works!
// that means no changes are necessary below this line! And if you choose to change something
// keep it compatible with what was provided to you!

bool addNewTile() {
	game.activeTile.y = 0;
	game.activeTile.x = (game.grid.x - 1) / 2;
	if (tileOccupied(game.activeTile))
		return false;
	newTile(game.activeTile);
	return true;
}

bool moveRight() {
	coord const newTile = {game.activeTile.x + 1, game.activeTile.y};
	if (game.activeTile.x < (game.grid.x - 1) && !tileOccupied(newTile)) {
		copyTile(newTile, game.activeTile);
		resetTile(game.activeTile);
		game.activeTile = newTile;
		return true;
	}
	return false;
}

bool moveLeft() {
	coord const newTile = {game.activeTile.x - 1, game.activeTile.y};
	if (game.activeTile.x > 0 && !tileOccupied(newTile)) {
		copyTile(newTile, game.activeTile);
		resetTile(game.activeTile);
		game.activeTile = newTile;
		return true;
	}
	return false;
}


bool moveDown() {
	coord const newTile = {game.activeTile.x, game.activeTile.y + 1};
	if (game.activeTile.y < (game.grid.y - 1) && !tileOccupied(newTile)) {
		copyTile(newTile, game.activeTile);
		resetTile(game.activeTile);
		game.activeTile = newTile;
		return true;
	}
	return false;
}


bool clearRow() {
	if (rowOccupied(game.grid.y - 1)) {
		for (unsigned int y = game.grid.y - 1; y > 0; y--) {
			copyRow(y, y - 1);
		}
		resetRow(0);
		return true;
	}
	return false;
}

void advanceLevel() {
	game.level++;
	switch(game.nextGameTick) {
	case 1:
		break;
	case 2 ... 10:
		game.nextGameTick--;
		break;
	case 11 ... 20:
		game.nextGameTick -= 2;
		break;
	default:
		game.nextGameTick -= 10;
	}
}

void newGame() {
	game.state = ACTIVE;
	game.tiles = 0;
	game.rows = 0;
	game.score = 0;
	game.tick = 0;
	game.level = 0;
	resetPlayfield();
}

void gameOver() {
	game.state = GAMEOVER;
	game.nextGameTick = game.initNextGameTick;
}


bool sTetris(int const key) {
	bool playfieldChanged = false;

	if (game.state & ACTIVE) {
		// Move the current tile
		if (key) {
			playfieldChanged = true;
			switch(key) {
			case KEY_LEFT:
				moveLeft();
				break;
			case KEY_RIGHT:
				moveRight();
				break;
			case KEY_DOWN:
				while (moveDown()) {};
				game.tick = 0;
				break;
			default:
				playfieldChanged = false;
			}
		}

		// If we have reached a tick to update the game
		if (game.tick == 0) {
			// We communicate the row clear and tile add over the game state
			// clear these bits if they were set before
			game.state &= ~(ROW_CLEAR | TILE_ADDED);

			playfieldChanged = true;
			// Clear row if possible
			if (clearRow()) {
				game.state |= ROW_CLEAR;
				game.rows++;
				game.score += game.level + 1;
				if ((game.rows % game.rowsPerLevel) == 0) {
					advanceLevel();
				}
			}

			// if there is no current tile or we cannot move it down,
			// add a new one. If not possible, game over.
			if (!tileOccupied(game.activeTile) || !moveDown()) {
				if (addNewTile()) {
					game.state |= TILE_ADDED;
					game.tiles++;
				} else {
					gameOver();
				}
			}
		}
	}

	// Press any key to start a new game
	if ((game.state == GAMEOVER) && key) {
		playfieldChanged = true;
		newGame();
		addNewTile();
		game.state |= TILE_ADDED;
		game.tiles++;
	}

	return playfieldChanged;
}

int readKeyboard() {
	struct pollfd pollStdin = {
			 .fd = STDIN_FILENO,
			 .events = POLLIN
	};
	int lkey = 0;

	if (poll(&pollStdin, 1, 0)) {
		lkey = fgetc(stdin);
		if (lkey != 27)
			goto exit;
		lkey = fgetc(stdin);
		if (lkey != 91)
			goto exit;
		lkey = fgetc(stdin);
	}
 exit:
		switch (lkey) {
			case 10: return KEY_ENTER;
			case 65: return KEY_UP;
			case 66: return KEY_DOWN;
			case 67: return KEY_RIGHT;
			case 68: return KEY_LEFT;
		}
	return 0;
}

void renderConsole(bool const playfieldChanged) {
	if (!playfieldChanged)
		return;

	// Goto beginning of console
	fprintf(stdout, "\033[%d;%dH", 0, 0);
	for (unsigned int x = 0; x < game.grid.x + 2; x ++) {
		fprintf(stdout, "-");
	}
	fprintf(stdout, "\n");
	for (unsigned int y = 0; y < game.grid.y; y++) {
		fprintf(stdout, "|");
		for (unsigned int x = 0; x < game.grid.x; x++) {
			coord const checkTile = {x, y};
			fprintf(stdout, "%c", (tileOccupied(checkTile)) ? '#' : ' ');
		}
		switch (y) {
			case 0:
				fprintf(stdout, "| Tiles: %10u\n", game.tiles);
				break;
			case 1:
				fprintf(stdout, "| Rows:  %10u\n", game.rows);
				break;
			case 2:
				fprintf(stdout, "| Score: %10u\n", game.score);
				break;
			case 4:
				fprintf(stdout, "| Level: %10u\n", game.level);
				break;
			case 7:
				fprintf(stdout, "| %17s\n", (game.state == GAMEOVER) ? "Game Over" : "");
				break;
		default:
				fprintf(stdout, "|\n");
		}
	}
	for (unsigned int x = 0; x < game.grid.x + 2; x++) {
		fprintf(stdout, "-");
	}
	fflush(stdout);
}


inline unsigned long uSecFromTimespec(struct timespec const ts) {
	return ((ts.tv_sec * 1000000) + (ts.tv_nsec / 1000));
}

int main(int argc, char **argv) {
	(void) argc;
	(void) argv;
	// This sets the stdin in a special state where each
	// keyboard press is directly flushed to the stdin and additionally
	// not outputted to the stdout
	{
		struct termios ttystate;
		tcgetattr(STDIN_FILENO, &ttystate);
		ttystate.c_lflag &= ~(ICANON | ECHO);
		ttystate.c_cc[VMIN] = 1;
		tcsetattr(STDIN_FILENO, TCSANOW, &ttystate);
	}

	// Allocate the playing field structure
	game.rawPlayfield = (tile *) malloc(game.grid.x * game.grid.y * sizeof(tile));
	game.playfield = (tile**) malloc(game.grid.y * sizeof(tile *));
	if (!game.playfield || !game.rawPlayfield) {
		fprintf(stderr, "ERROR: could not allocate playfield\n");
		return 1;
	}
	for (unsigned int y = 0; y < game.grid.y; y++) {
		game.playfield[y] = &(game.rawPlayfield[y * game.grid.x]);
	}

	// Reset playfield to make it empty
	resetPlayfield();
	// Start with gameOver
	gameOver();

	if (!initializeSenseHat()) {
		fprintf(stderr, "ERROR: could not initilize sense hat\n");
		return 1;
	};

	// Clear console, render first time
	fprintf(stdout, "\033[H\033[J");
	renderConsole(true);
	renderSenseHatMatrix(true);

	while (true) {
		struct timeval sTv, eTv;
		gettimeofday(&sTv, NULL);

		int key = readSenseHatJoystick();
		if (!key)
			key = readKeyboard();
		if (key == KEY_ENTER)
			break;

		bool playfieldChanged = sTetris(key);
		renderConsole(playfieldChanged);
		renderSenseHatMatrix(playfieldChanged);

		// Wait for next tick
		gettimeofday(&eTv, NULL);
		unsigned long const uSecProcessTime = ((eTv.tv_sec * 1000000) + eTv.tv_usec) - ((sTv.tv_sec * 1000000 + sTv.tv_usec));
		if (uSecProcessTime < game.uSecTickTime) {
			usleep(game.uSecTickTime - uSecProcessTime);
		}
		game.tick = (game.tick + 1) % game.nextGameTick;
	}

	freeSenseHat();
	free(game.playfield);
	free(game.rawPlayfield);

	return 0;
}
