#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include "FS.h"
#include "SPIFFS.h"

#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33

#define FORMAT_SPIFFS_IF_FAILED true

// Screen definitions
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 320
#define BLOCK_SIZE 12 // adjust as you like
#define GRID_COLS 10
#define GRID_ROWS 20
#define OFFSET_X ((SCREEN_WIDTH - (GRID_COLS * BLOCK_SIZE)) / 2)
#define OFFSET_Y 15
#define COLOR_BG TFT_BLACK

// Control buttons
#define BUTTON_HEIGHT 40
#define BUTTON_WIDTH (SCREEN_WIDTH / 3)
#define BUTTON_Y (SCREEN_HEIGHT - BUTTON_HEIGHT - 5)

// Game definitions
#define MOVE_INTERVAL 750          // Initially 750 ms
#define MOVE_INTERVAL_MIN 150      // minimum 150 ms
#define MOVE_INTERVAL_PERIOD 60000 // 1 minute
#define MOVE_INTERVAL_DECR 100     // 100 ms decrements

unsigned long lastMoveTime = 0;
unsigned long gameStart = 0;
unsigned long moveInterval = MOVE_INTERVAL; // 500 ms drop speed
int score = 0;
int highScore = 0;

TFT_eSPI tft = TFT_eSPI();
SPIClass touchscreenSPI = SPIClass(VSPI);
XPT2046_Touchscreen touchscreen(XPT2046_CS, XPT2046_IRQ);

const uint8_t TETROMINOES[7][4][4] = {
    // I
    {
        {0, 0, 0, 0},
        {1, 1, 1, 1},
        {0, 0, 0, 0},
        {0, 0, 0, 0}},

    // J
    {
        {1, 0, 0, 0},
        {1, 1, 1, 0},
        {0, 0, 0, 0},
        {0, 0, 0, 0}},

    // L
    {
        {0, 0, 1, 0},
        {1, 1, 1, 0},
        {0, 0, 0, 0},
        {0, 0, 0, 0}},

    // O
    {
        {0, 1, 1, 0},
        {0, 1, 1, 0},
        {0, 0, 0, 0},
        {0, 0, 0, 0}},

    // S
    {
        {0, 1, 1, 0},
        {1, 1, 0, 0},
        {0, 0, 0, 0},
        {0, 0, 0, 0}},

    // T
    {
        {0, 1, 0, 0},
        {1, 1, 1, 0},
        {0, 0, 0, 0},
        {0, 0, 0, 0}},

    // Z
    {
        {1, 1, 0, 0},
        {0, 1, 1, 0},
        {0, 0, 0, 0},
        {0, 0, 0, 0}}};

uint16_t grid[GRID_ROWS][GRID_COLS];

const uint16_t TETROMINO_COLORS[7] = {
    TFT_CYAN, TFT_BLUE, TFT_ORANGE, TFT_YELLOW, TFT_GREEN, TFT_MAGENTA, TFT_RED};

bool havePiece = false;

int buttonWidth = (GRID_COLS * BLOCK_SIZE) / 3;

unsigned long lastButtonTime = 0;
unsigned long buttonInterval = 150; // 150 ms between moves

typedef struct
{
  uint8_t shape[4][4]; // 4x4 matrix
  int posX;            // X position on grid
  int posY;            // Y position on grid
  uint16_t color;      // Color for this piece
} Tetromino;

Tetromino currentPiece = {
    {{0, 1, 0, 0},
     {1, 1, 1, 0},
     {0, 0, 0, 0},
     {0, 0, 0, 0}},
    4,      // starting X position
    0,      // starting Y position
    TFT_RED // color
};

void drawPlayfieldBorder()
{
  tft.drawRect(OFFSET_X - 2, OFFSET_Y - 2,
               GRID_COLS * BLOCK_SIZE + 4,
               GRID_ROWS * BLOCK_SIZE + 4, TFT_WHITE);
}

void drawTouchButtons()
{
  int x = 0;
  tft.fillRect(x, BUTTON_Y, BUTTON_WIDTH, BUTTON_HEIGHT, TFT_DARKGREY);
  tft.drawRect(x, BUTTON_Y, BUTTON_WIDTH, BUTTON_HEIGHT, TFT_WHITE);
  tft.setCursor(x + 20, BUTTON_Y + 12);
  tft.setTextColor(TFT_WHITE);
  tft.print("LEFT");

  x += BUTTON_WIDTH;
  tft.fillRect(x, BUTTON_Y, BUTTON_WIDTH, BUTTON_HEIGHT, TFT_DARKGREY);
  tft.drawRect(x, BUTTON_Y, BUTTON_WIDTH, BUTTON_HEIGHT, TFT_WHITE);
  tft.setCursor(x + 20, BUTTON_Y + 12);
  tft.print("ROTATE");

  x += BUTTON_WIDTH;
  tft.fillRect(x, BUTTON_Y, BUTTON_WIDTH, BUTTON_HEIGHT, TFT_DARKGREY);
  tft.drawRect(x, BUTTON_Y, BUTTON_WIDTH, BUTTON_HEIGHT, TFT_WHITE);
  tft.setCursor(x + 20, BUTTON_Y + 12);
  tft.print("RIGHT");
}

void drawNewGameButton()
{
  int x = BUTTON_WIDTH;
  int y = OFFSET_Y + GRID_ROWS * BLOCK_SIZE / 2 + 15;
  tft.fillRect(x, y, BUTTON_WIDTH, BUTTON_HEIGHT, TFT_DARKGREY);
  tft.drawRect(x, y, BUTTON_WIDTH, BUTTON_HEIGHT, TFT_WHITE);
  tft.setTextSize(1);
  tft.setCursor(x + 20, y + 12);
  tft.setTextColor(TFT_WHITE);
  tft.print("NEW GAME");
}

void drawScore()
{
  tft.fillRect(0, 0, SCREEN_WIDTH, 10, COLOR_BG); // Clear score area
  tft.setCursor(5, 0);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.print("Score: ");
  tft.print(score);
}

void drawHighScore()
{
  highScore = loadHighScore();
  tft.fillRect(0, BUTTON_Y - 10, SCREEN_WIDTH, 10, COLOR_BG); // Clear score area
  tft.setCursor(5, BUTTON_Y - 10);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.print("High Score: ");
  tft.print(highScore);
}

void newGame()
{
  // Clear TFT screen
  tft.fillScreen(COLOR_BG);
  clearGrid();
  drawPlayfieldBorder();
  drawTouchButtons();

  score = 0;
  drawScore();
  drawHighScore();
  
  randomSeed(micros());
  createRandomTetromino(&currentPiece);

  gameStart = 0;
  moveInterval = MOVE_INTERVAL;
}

void setup()
{
  // put your setup code here, to run once:
  tft.begin();
  tft.setRotation(2);

  touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  touchscreen.begin(touchscreenSPI);
  touchscreen.setRotation(2);

  Serial.begin(115200); // Debug only

  if (!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED))
  {
    Serial.println("SPIFFS Mount Failed");
    return;
  }
  else
  {
    Serial.printf("SPIFFS has begun\n");
  }

  drawNewGameButton();
  delay(100);
  while (true)
  {
    if (handleNewGameButton())
      break;
    delay(20);
  }
}

void testGrid()
{
  grid[0][0] = TFT_BLUE;
  grid[0][GRID_COLS - 1] = TFT_MAGENTA;
  grid[GRID_ROWS - 1][0] = TFT_RED;
  grid[GRID_ROWS - 1][GRID_COLS - 1] = TFT_CYAN;
  drawGrid(BLOCK_SIZE, OFFSET_X, OFFSET_Y, COLOR_BG);
}

void drawTetromino(Tetromino *tet, uint16_t blockSize, int offsetX, int offsetY)
{
  for (int y = 0; y < 4; y++)
  {
    for (int x = 0; x < 4; x++)
    {
      if (tet->shape[y][x])
      {
        int px = offsetX + (tet->posX + x) * blockSize;
        int py = offsetY + (tet->posY + y) * blockSize;
        tft.fillRect(px, py, blockSize - 1, blockSize - 1, tet->color);
      }
    }
  }
}

void clearTetromino(Tetromino *tet, uint16_t blockSize, int offsetX, int offsetY, uint16_t bgColor)
{
  for (int y = 0; y < 4; y++)
  {
    for (int x = 0; x < 4; x++)
    {
      if (tet->shape[y][x])
      {
        int px = offsetX + (tet->posX + x) * blockSize;
        int py = offsetY + (tet->posY + y) * blockSize;
        tft.fillRect(px, py, blockSize - 1, blockSize - 1, bgColor);
      }
    }
  }
}

void rotateTetromino(Tetromino *tet)
{
  uint8_t temp[4][4];

  for (int y = 0; y < 4; y++)
  {
    for (int x = 0; x < 4; x++)
    {
      temp[x][3 - y] = tet->shape[y][x];
    }
  }

  for (int y = 0; y < 4; y++)
  {
    for (int x = 0; x < 4; x++)
    {
      tet->shape[y][x] = temp[y][x];
    }
  }
}

void printTetromino(unsigned tet[4][4])
{

  printf("\n");
  for (int i = 0; i < 4; i++)
  {
    for (int j = 0; j < 4; j++)
    {
      printf("%d  ", tet[i][j]);
    }
    printf("\n");
  }
}

void createRandomTetromino(Tetromino *tet)
{
  int randomIndex = random(0, 7); // 0 to 6

  // Copy shape
  for (int y = 0; y < 4; y++)
  {
    for (int x = 0; x < 4; x++)
    {
      tet->shape[y][x] = TETROMINOES[randomIndex][y][x];
    }
  }

  // Starting position (centered at top)
  tet->posX = 3;
  tet->posY = 0;

  // Assign color
  tet->color = TETROMINO_COLORS[randomIndex];
}

bool placeTetrominoToGrid(Tetromino *tet)
{

  for (int y = 0; y < 4; y++)
  {
    for (int x = 0; x < 4; x++)
    {
      if (tet->shape[y][x])
      {
        int gx = tet->posX + x;
        int gy = tet->posY + y;

        if (gy >= 0 && gy < GRID_ROWS && gx >= 0 && gx < GRID_COLS)
        {
          grid[gy][gx] = tet->color;
        }
      }
    }
  }

  return false;
}

void drawGrid(uint16_t blockSize, int offsetX, int offsetY, uint16_t bgColor)
{
  for (int y = 0; y < GRID_ROWS; y++)
  {
    for (int x = 0; x < GRID_COLS; x++)
    {
      int px = offsetX + x * blockSize;
      int py = offsetY + y * blockSize;

      uint16_t color = grid[y][x] ? grid[y][x] : bgColor;
      tft.fillRect(px, py, blockSize - 1, blockSize - 1, color);
    }
  }
}

void clearGrid()
{
  for (int y = 0; y < GRID_ROWS; y++)
  {
    for (int x = 0; x < GRID_COLS; x++)
    {
      grid[y][x] = 0;
    }
  }
}

bool checkCollision(Tetromino *tet, int deltaX, int deltaY)
{
  for (int y = 0; y < 4; y++)
  {
    for (int x = 0; x < 4; x++)
    {
      if (tet->shape[y][x])
      {
        int newX = tet->posX + x + deltaX;
        int newY = tet->posY + y + deltaY;

        // Check for collision with existing blocks
        if (newY >= 0 && grid[newY][newX])
        {
          return true;
        }

        // Check for out of bounds
        if (newX < 0 || newX >= GRID_COLS || newY >= GRID_ROWS)
        {
          return true;
        }
      }
    }
  }
  return false;
}

void clearFullLines()
{

  int completeLinesCount = 0;

  for (int y = GRID_ROWS - 1; y > 0; y--)
  {

    bool isComplete = true;
    for (int x = 0; x < GRID_COLS; x++)
      if (grid[y][x] == 0)
      {
        isComplete = false;
        break;
      }

    if (isComplete)
    {
      completeLinesCount++;
      // Move everything above down
      for (int yy = y; yy > 0; yy--)
      {
        for (int x = 0; x < GRID_COLS; x++)
        {
          grid[yy][x] = grid[yy - 1][x];
        }
      }

      // Clear top line
      for (int x = 0; x < GRID_COLS; x++)
      {
        grid[0][x] = 0;
      }
    }
  }
  switch (completeLinesCount)
  {
  case 0:
    break;
  case 1:
    score += 100;
    break;
  case 2:
    score += 300;
    break;
  case 3:
    score += 500;
    break;
  case 4:
    score += 800;
    break;
  default:
    break;
  }
  // Redraw grid
  drawGrid(BLOCK_SIZE, OFFSET_X, OFFSET_Y, COLOR_BG);
  drawScore();
}

void handleTouchInput()
{
  uint16_t x, y;
  if (touchscreen.tirqTouched() && touchscreen.touched())
  {
    TS_Point p = touchscreen.getPoint();
    // Calibrate Touchscreen points with map function to the correct width and height
    x = map(p.x, 200, 3700, 1, SCREEN_WIDTH);
    y = map(p.y, 240, 3800, 1, SCREEN_HEIGHT);

    unsigned long now = millis();
    if (now - lastButtonTime < buttonInterval)
      return;

    if (y > BUTTON_Y && y < BUTTON_Y + BUTTON_HEIGHT)
    {

      if (x < BUTTON_WIDTH)
      {
        // LEFT
        if (!checkCollision(&currentPiece, -1, 0))
        {
          clearTetromino(&currentPiece, BLOCK_SIZE, OFFSET_X, OFFSET_Y, COLOR_BG);
          currentPiece.posX--;
          drawTetromino(&currentPiece, BLOCK_SIZE, OFFSET_X, OFFSET_Y);
        }
      }
      else if (x < 2 * BUTTON_WIDTH)
      {
        // ROTATE
        Tetromino temp = currentPiece;
        rotateTetromino(&temp);
        if (!checkCollision(&temp, 0, 0))
        {
          clearTetromino(&currentPiece, BLOCK_SIZE, OFFSET_X, OFFSET_Y, COLOR_BG);
          rotateTetromino(&currentPiece);
          drawTetromino(&currentPiece, BLOCK_SIZE, OFFSET_X, OFFSET_Y);
        }
      }
      else if (x < 3 * BUTTON_WIDTH)
      {
        // RIGHT
        if (!checkCollision(&currentPiece, 1, 0))
        {
          clearTetromino(&currentPiece, BLOCK_SIZE, OFFSET_X, OFFSET_Y, COLOR_BG);
          currentPiece.posX++;
          drawTetromino(&currentPiece, BLOCK_SIZE, OFFSET_X, OFFSET_Y);
        }
      }
    }

    if ((x > OFFSET_X - 2) && (x < OFFSET_X + GRID_COLS * BLOCK_SIZE + 2) && (y > OFFSET_Y - 2) && (y < OFFSET_Y + GRID_ROWS * BLOCK_SIZE + 2))
    {
      // move down the current piece quicker
      moveDown();
    }

    lastButtonTime = now;
  }
}

bool handleNewGameButton()
{

  int x, y;
  if (touchscreen.tirqTouched() && touchscreen.touched())
  {
    TS_Point p = touchscreen.getPoint();
    // Calibrate Touchscreen points with map function to the correct width and height
    x = map(p.x, 200, 3700, 1, SCREEN_WIDTH);
    y = map(p.y, 240, 3800, 1, SCREEN_HEIGHT);

    if ((x > BUTTON_WIDTH) && (x < x + BUTTON_WIDTH) && (y > OFFSET_Y + GRID_ROWS * BLOCK_SIZE / 2 + 15) && (y < OFFSET_Y + GRID_ROWS * BLOCK_SIZE / 2 + 15 + BUTTON_HEIGHT))
    {
      newGame();
      return true;
    }
  }

  return false;
}

void moveDown()
{
  if (!checkCollision(&currentPiece, 0, 1))
  {
    // No collision — move piece down
    clearTetromino(&currentPiece, BLOCK_SIZE, OFFSET_X, OFFSET_Y, COLOR_BG);
    currentPiece.posY++;
    drawTetromino(&currentPiece, BLOCK_SIZE, OFFSET_X, OFFSET_Y);
  }
  else
  {
    // Collision detected — lock piece into grid
    placeTetrominoToGrid(&currentPiece);
    drawGrid(BLOCK_SIZE, OFFSET_X, OFFSET_Y, COLOR_BG);

    // Spawn a new random piece
    createRandomTetromino(&currentPiece);

    // Check if new piece immediately collides (game over)
    if (checkCollision(&currentPiece, 0, 0))
    {
      tft.setTextColor(TFT_RED, COLOR_BG);
      tft.setTextSize(2);
      tft.setTextDatum(MC_DATUM);
      tft.drawString("GAME OVER", SCREEN_WIDTH / 2, OFFSET_Y + GRID_ROWS * BLOCK_SIZE / 2 - 20);

      if (score > highScore)
      {
        tft.setTextColor(TFT_CYAN, COLOR_BG);
        tft.setTextSize(3);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("HIGH SCORE!", SCREEN_WIDTH / 2, OFFSET_Y + 25);

        saveHighScore(score);
        highScore = score;
      }

      drawNewGameButton();
      while (true)
        if (handleNewGameButton())
          break;
      delay(20);
    }

    drawTetromino(&currentPiece, BLOCK_SIZE, OFFSET_X, OFFSET_Y);
  }
}

void loop()
{
  unsigned long now = millis();
  if (!gameStart)
    gameStart = now;

  // Speed up the game every minute till moveInterval hits 200ms
  if (moveInterval > MOVE_INTERVAL_MIN)
  {
    if ((now - gameStart) > MOVE_INTERVAL_PERIOD)
    {
      moveInterval -= MOVE_INTERVAL_DECR;
      gameStart = now;

      Serial.print("New interval:");
      Serial.println(moveInterval);
    }
  }

  clearFullLines();

  handleTouchInput();

  if (now - lastMoveTime > moveInterval)
  {
    moveDown();
    drawScore();

    lastMoveTime = now;
  }
}

void saveHighScore(int highScore)
{
  fs::File file = SPIFFS.open("/highscore.txt", FILE_WRITE);
  if (file)
  {
    file.print(highScore);
    file.close();
  }
}

int loadHighScore()
{
  Serial.printf("Loading high score...");
  // Now read it back
  fs::File file = SPIFFS.open("/highscore.txt");
  int hs = 0;
  if (file)
  {
    hs = file.parseInt();
    file.close();
  }
  return hs;
}
