#include <avr/io.h>
#include <util/delay.h>
#include <stdio.h>
#include <stdlib.h>
#include <avr/interrupt.h>

#include "lcd.h"
#include "graphics.h"
#include "cpu_speed.h"
#include "sprite.h"

#include "usb_serial.h"

#include "math.h"

#define BUFF_LENGTH 20

#define FREQUENCY 8000000.0
#define PRESCALER 1024.0

#define TWOTOSIXTEEN 65536

#define NUM_BUTTONS 6
#define BTN_DPAD_LEFT 0
#define BTN_DPAD_RIGHT 1
#define BTN_DPAD_UP 2
#define BTN_DPAD_DOWN 3
#define BTN_LEFT 4
#define BTN_RIGHT 5

#define BTN_STATE_UP 0
#define BTN_STATE_DOWN 1

#define LINE_LENGTH 5

#define CRAFT_SPEED 1

#define BULLET_COUNT 5
#define ALIEN_COUNT 5

#define BULLET_SPEED 1.5
#define ALIEN_SPEED 0.8

#define MOTHERSHIP_SPEED 0.5;

#define COMPARE_TIMER_SPEED 3906.25 // 0.5s * (FREQUENCY/PRESCALER)

unsigned char bullet[2] = {
	0b11000000,
	0b11000000
};

unsigned char craft[5] = {
	0b00100000,
	0b01110000,
	0b11111000,
	0b01110000,
	0b00100000
};

unsigned char alien[5] = {
	0b01110000,
	0b00100000,
	0b11111000,
	0b00100000,
	0b01110000
};

unsigned char mothership[16] = {
	0b11111111, 0b11000000,
	0b10011110, 0b01000000,
	0b10011110, 0b01000000,
	0b11111111, 0b11000000,
	0b11011110, 0b11000000,
	0b11011110, 0b11000000,
	0b11011110, 0b11000000,
	0b11000000, 0b11000000
};

Sprite craft_sprite;
Sprite bullet_sprite[BULLET_COUNT];
Sprite alien_sprite[ALIEN_COUNT];
Sprite mothership_sprite;
Sprite mothership_bullet;

double mothership_fire = 0;
double mothership_wait = -5;
double alien_wait[ALIEN_COUNT] = {0, 0, 0, 0, 0};
char on_wall[ALIEN_COUNT] = {0, 0, 0, 0, 0};
char m_on_wall = 0;

double craft_previous_time = 0;
double alien_previous_time = 0;
double bullet_previous_time = 0;

double previous_time = 0;
double difference = 0;

int mothership_lives = 10;
int speed = 0;
int aim_x, aim_y;
double sp_count = 0;

char boss_time = 1;

void shoot(int degrees);
void send_line(char* string);
void send_debug_string(char* string);
double get_global_time();

/* 	
	0: BTN_DPAD_LEFT 	1: BTN_DPAD_RIGHT 
	2: BTN_DPAD_UP 		3: BTN_DPAD_DOWN 
	4: BTN_LEFT 		5: BTN_RIGHT 	
	6: gameRunning 		7: gamePaused
*/
int pre_press = 0;
int score = 0;
int lives = 3;

int seconds = 0;
int minutes = 0;

char gameRunning = 0;

int press_count;
unsigned long overflow_count;
int debug_overflow_count = 0;
char buff[BUFF_LENGTH];

volatile unsigned char btn_hists[NUM_BUTTONS];
volatile unsigned char btn_held[NUM_BUTTONS];

void init_sprites();
double angle_to(double x2, double y2, double x1, double y1);
char alien_collided_craft(int i);
char craft_collided_alien();
char has_collided_sprite(Sprite* sprite, Sprite* spr);

void init_hardware(){

	// Default Contrast
	lcd_init(LCD_DEFAULT_CONTRAST);

	usb_init();

	ADMUX |= 1<<REFS0;
	ADMUX &= ~(1<<REFS1);
	//enable adc
	ADCSRA |= 1<<ADEN;

	//set prescaler
	ADCSRA |= (1<<2)|(1<<1)|1;

	// Init Buttons as input
	DDRF &= ~((1 << PF5) | (1 << PF6));
	DDRD &= ~((1 << PD1) | (1 << PD0));
	DDRB &= ~((1 << PB7) | (1 << PB1));

	// Init LED as output
	DDRB |= ((1 << PB2) | (1 << PB3)); 

	

	// Init Timer 0 (Prescaler 256 (~8ms))

	TCCR0B &= ~(1<<WGM02);

	TCCR0B |= (1<<CS00);
	TCCR0B &= ~(1<<CS01);
	TCCR0B |= (1<<CS02);

	TIMSK0 |= 1<<TOIE0;

	// Init Timer 1 (Prescaler 1024 (~8.3s))

	TCCR1B &= ~(1<<WGM12);

	TCCR1B |= (1<<CS10);
	TCCR1B &= ~(1<<CS11);
	TCCR1B |= (1<<CS12);

	TIMSK1 |= 1<<TOIE1;

	// Init Timer 3 (Prescaler )

	TCCR3A |= (1<<COM3A1);
	TCCR3B |= (1<<WGM32);
	TCCR3B |= (1<<CS30);
	TCCR3B &= ~(1<<CS31);
	TCCR3B |= (1<<CS32);

	TIMSK3 |= 1<<OCIE3A;
	OCR3A = 3906.25;

	// Timer 4 Compare Mode

	TCCR4B &= ~(1<<CS40);
	TCCR4B &= ~(1<<CS41);
	TCCR4B &= ~(1<<CS42);
	TCCR4B |= (1<<CS43);

	TIMSK4 |= 1<<TOIE4;



	// Globally enable interrupts
	sei();


}

/*  
*	CAB202 Tutorial 10 (Question 2_3)
*	B.Talbot, September 2015
*	Queensland University of Technology
*/
void send_debug_string(char* string) {
	// Send the debug preamble...
	sprintf(buff, "[DEBUG @ %03.03f] ;", (double)get_global_time());
	//usb_serial_write(buff, 18);

	// Send all of the characters in the string

	for(int i = 0; i < BUFF_LENGTH; i++){
		if(buff[i] == ';') break;
		usb_serial_putchar(buff[i]);
	}

	unsigned char char_count = 0;
	while (*string != '\0') {
		usb_serial_putchar(*string);
		string++;
		char_count++;
	}

	// Go to a new line (force this to be the start of the line)
	usb_serial_putchar('\r');
	usb_serial_putchar('\n');
 }

void materialise_spaceship(){
	craft_sprite.is_visible = 1;

	craft_sprite.x = rand() % (LCD_X - 7) + 1;
	craft_sprite.y = rand() % (LCD_Y - 16) + 10;

	while(craft_collided_alien()){
		craft_sprite.x = rand() % (LCD_X - 7) + 1;
		craft_sprite.y = rand() % (LCD_Y - 16) + 10;
	}
}

char aliens_dead(){
	for (int i = 0; i < ALIEN_COUNT; i++){
		if(alien_sprite[i].is_visible){
			return 0;
		}
	}
	return 1;
}

void materialise_alien(int alien){
	alien_sprite[alien].is_visible = 1;
	double ok = -((double)((rand() % 200) + 200)/100.0);
	alien_sprite[alien].dx = 0;
	alien_sprite[alien].dy = 0;
	alien_wait[alien] = ok;
	alien_sprite[alien].x = rand() % (LCD_X - 7) + 1;
	alien_sprite[alien].y = rand() % (LCD_Y - 16) + 10;
	while(alien_collided_craft(alien)){
		alien_sprite[alien].x = rand() % (LCD_X - 7) + 1;
		alien_sprite[alien].y = rand() % (LCD_Y - 16) + 10;
	}

	//sprintf(buff, "%f", alien_wait[alien]);
	//send_line(buff);

}

void materialise_aliens(){
	for(int i = 0; i < ALIEN_COUNT; i++){
		materialise_alien(i);
	}

	/* sprintf(aff, "Hello %f:%f:%f:%f:%f", alien_wait[0], alien_wait[1], alien_wait[2], alien_wait[3], alien_wait[4]);
	send_line(aff); */

}

void materialise_boss(){
	mothership_sprite.is_visible = 1;
	mothership_sprite.dx = 0;
	mothership_sprite.dy = 0;
	mothership_wait = -((double)((rand() % 200) + 200)/100.0);
	mothership_fire = -((double)((rand() % 200) + 200)/100.0);
	mothership_sprite.x = rand() % (LCD_X - 11) + 1;
	mothership_sprite.y = rand() % (LCD_Y - 18) + 10;
	while(has_collided_sprite(&mothership_sprite, &craft_sprite)){
		mothership_sprite.x = rand() % (LCD_X - 11) + 1;
		mothership_sprite.y = rand() % (LCD_Y - 19) + 10;
	}
}

void draw_boss_health(){
	if(mothership_sprite.y > 12){
		draw_line(mothership_sprite.x, mothership_sprite.y - 2, mothership_sprite.x + mothership_lives - 1, mothership_sprite.y - 2);
	}
	else{
		draw_line(mothership_sprite.x, mothership_sprite.y + 10, mothership_sprite.x + mothership_lives - 1, mothership_sprite.y + 10);
	}
}

void boss_shoot(){
	double angle = angle_to(mothership_sprite.x + 5, mothership_sprite.y + 4, craft_sprite.x + 2, craft_sprite.y + 2);
	if(!mothership_bullet.is_visible){
		mothership_bullet.is_visible = 1;
		mothership_bullet.x = mothership_sprite.x + 5;
		mothership_bullet.y = mothership_sprite.y + 4;
		mothership_bullet.dx = cos(angle) * BULLET_SPEED;
		mothership_bullet.dy = sin(angle) * BULLET_SPEED;
		mothership_fire = -((double)((rand() % 200) + 200)/100.0);
	}
}

void boss_battle(){
	materialise_boss();
}

void init_variables(){

	pre_press = 0;
	press_count = 0;
	overflow_count = 0;
	score = 0;
	lives = 3;
	TCNT0 = 0;
	seconds = 0;
	minutes = 0;

	boss_time = 1;

	previous_time = 0;

	for (int i = 0; i < NUM_BUTTONS; i++){
		btn_held[i] = 0;
		btn_hists[i] = 0;
	}
}

void send_status(){
	char aff[80];
	sprintf(aff, "Location: ( %d, %d) Aim: %d",(int)(craft_sprite.x), (int)(craft_sprite.y), (int)((ADC * 0.705)));
	send_debug_string(aff);
}

void init_sprites(){
	init_sprite(&craft_sprite, 0, 0, 5, 5, craft);
	craft_sprite.is_visible = 0;
	init_sprite(&mothership_sprite, 0, 0, 10, 8, mothership);
	mothership_sprite.is_visible = 0;
	init_sprite(&mothership_bullet, 0, 0, 2, 2, bullet);
	mothership_bullet.is_visible = 0;

	for(int i = 0; i < ALIEN_COUNT; i++){
		init_sprite(&alien_sprite[i], 0, 40, 5, 5, alien);
		alien_sprite[i].is_visible = 0;
		init_sprite(&bullet_sprite[i], 0, 20, 2, 2, bullet);
		bullet_sprite[i].is_visible = 0;
	}
}

double get_global_time() {
	return (PRESCALER / FREQUENCY) * (TCNT1 + (debug_overflow_count * TWOTOSIXTEEN));
}

double get_system_time() {
	return (PRESCALER / FREQUENCY) * (TCNT0 + (overflow_count * 256));
}

void wait_for_press(){
	pre_press = press_count;
	while(1){
		show_screen();
		if (btn_held[BTN_LEFT] || btn_held[BTN_RIGHT]){
			break;
		}
	}
}

void process_time(){
	minutes = floor(get_system_time() / 60);
	seconds = (int) floor(get_system_time()) % 60;
}


void intro_menu(){
	clear_screen();
	draw_string(1, 0, "Alien Advance");
	draw_string(1, 8, "Bennett Hardwick");
	draw_string(1, 16, "n9803572");
	draw_string(1, 24, "Press a button");
	draw_string(1, 32, "to continue...");
	show_screen();
	wait_for_press();

	draw_string((LCD_X - 1)/2, 40, "3");
	show_screen();
	_delay_ms(300);
	draw_string((LCD_X - 1)/2, 40, "2");
	show_screen();
	_delay_ms(300);
	draw_string((LCD_X - 1)/2, 40, "1");
	show_screen();
	_delay_ms(300);
}

void draw_status_border(){

	// this is status

	sprintf(buff, "T:%02d:%02d L:%d S:%d", minutes, seconds, lives, score);
	draw_string(0, 0, buff);

	if(fmod(get_system_time(), 1) == 0.0){
		speed = sp_count;
		sp_count = 0;
	}

	// draw border

	draw_line(0, 9, LCD_X - 1, 9);
	draw_line(LCD_X - 1, 9, LCD_X - 1, LCD_Y - 1);
	draw_line(LCD_X - 1, LCD_Y - 1, 0, LCD_Y - 1);
	draw_line(0, LCD_Y - 1, 0, 9);
}	

void clear_game_screen(){
	for (int x = 1; x < LCD_X - 1; x++){
		for ( int y = 10; y < LCD_Y - 1; y++){
			set_pixel(x, y, 0);
		}
	}
}

void draw_aim_line(int degrees){
	double radians = degrees * M_PI / 180;
	int x = craft_sprite.x + 2;
	int y = craft_sprite.y + 2;
	int pixel_length = LINE_LENGTH;

	aim_x = x + cos(radians) * LINE_LENGTH;
	aim_y = y + sin(radians) * LINE_LENGTH;
	while(!(aim_x < LCD_X && aim_x > 0)){
		pixel_length--;
		aim_x = x + cos(radians) * pixel_length;
	}
	while(!(aim_y < LCD_Y && aim_y > 8)){
		pixel_length--;
		aim_y = y + sin(radians) * pixel_length;
	}
	draw_line(x, y, aim_x, aim_y);
}

// Taken from tutorial code (TUT10)
//	B.Talbot, September 2015
//	Queensland University of Technology
void draw_centred(unsigned char y, char* string) {
	// Draw a string centred in the LCD when you don't know the string length
	unsigned char l = 0, i = 0;
	while (string[i] != '\0') {
		l++;
		i++;
	}
	char x = 42-(l*5/2);
	draw_string((x > 0) ? x : 0, y, string);
}

void send_line(char* string) {
    // Send all of the characters in the string
    unsigned char char_count = 0;
    while (*string != '\0') {
        usb_serial_putchar(*string);
        string++;
        char_count++;
    }

    // Go to a new line (force this to be the start of the line)
    usb_serial_putchar('\r');
    usb_serial_putchar('\n');
}

// Modified version from the CAB202 Assignment 1 graphics library
//	B.Talbot, September 2015
//	Queensland University of Technology
char sprite_step( Sprite *sprite ) {
	int x0 = round( sprite->x );
	int y0 = round( sprite->y );
	sprite->x += sprite->dx;
	sprite->y += sprite->dy;
	int x1 = round( sprite->x );
	int y1 = round( sprite->y );
	return ( x1 != x0 ) || ( y1 != y0 );
}

char sprite_move( Sprite *sprite, double dx, double dy ) {
	int x0 = round( sprite->x );
	int y0 = round( sprite->y );
	sprite->x += dx;
	sprite->y += dy;
	int x1 = round( sprite->x );
	int y1 = round( sprite->y );
	return ( x1 != x0 ) || ( y1 != y0 );
}

void ADC_prep(){
	ADMUX &= ~((1<<MUX4)|(1<<MUX3)|(1<<MUX2)|(1<<MUX1));
	ADMUX |= (1<<MUX0);
	ADCSRA |= 1<<ADSC;
	// Wait for transfer
	while((ADCSRA>>ADSC)&1);
}

void process_input(){

	char a = usb_serial_getchar();

	//double timing = get_system_time() - craft_previous_time;

	if ((a == 'a' || btn_held[BTN_DPAD_LEFT]) && (craft_sprite.x > 1) ) craft_sprite.x += -(CRAFT_SPEED);
	if ((a == 'd' || btn_held[BTN_DPAD_RIGHT]) && (craft_sprite.x < LCD_X - 6) ) craft_sprite.x += (CRAFT_SPEED);
	if ((a == 'w' || btn_held[BTN_DPAD_UP]) && (craft_sprite.y > 10) ) craft_sprite.y += -(CRAFT_SPEED);
	if ((a == 's' || btn_held[BTN_DPAD_DOWN]) && (craft_sprite.y < LCD_Y - 6) ) craft_sprite.y += (CRAFT_SPEED);
	if ((a == ' ')) shoot(ADC * 0.705);

	//craft_previous_time = get_system_time();
	a = 0;
}

char has_collided_coords( Sprite* sprite, int x_s, int y_s){
	int x = (int) round( sprite->x );
	int y = (int) round( sprite->y );
	int offset = 0;

	for ( int row = 0; row < sprite->height; row++ ) {
		for ( int col = 0; col < sprite->width; col++ ) {
			char ch = sprite->bitmap[offset++] & 0xff;
			if ( ch != ' ' ) {
				if( x + col == x_s && y + row == y_s) return 1;
			}
		}
	}
	return 0;
}

char has_collided_sprite(Sprite* sprite, Sprite* spr ){
	if(sprite->is_visible && spr->is_visible){
		int x = (int) round( sprite->x );
		int y = (int) round( sprite->y );
		int offset = 0;
		for ( int row = 0; row < sprite->height; row++ ) {
			for ( int col = 0; col < sprite->width; col++ ) {
				char ch = sprite->bitmap[offset++] & 0xff;
				if ( ch != ' ' ) {
					if(has_collided_coords(spr, x + col, y + row)) return 1;
				}
			}
		}
	}
	
	return 0;
}

char alien_collided_craft(int i){
	if(has_collided_sprite(&craft_sprite, &alien_sprite[i])){
		return 1;
	}
	return 0;
}

char craft_collided_alien(){
	for ( int i = 0; i < ALIEN_COUNT; i++ ){
		if(has_collided_sprite(&craft_sprite, &alien_sprite[i])){
			return 1;
		}
	}
	return 0;
}

void check_collision(){
	for(int h = 0; h < BULLET_COUNT; h++){
		for(int i = 0; i < ALIEN_COUNT; i++){
			if(has_collided_sprite(&bullet_sprite[h], &alien_sprite[i])) {
				bullet_sprite[h].is_visible = 0; 
				alien_sprite[i].is_visible = 0; 
				send_debug_string("Player destroyed alien.");
				score++;
			}
		}
	}

	if(has_collided_sprite(&mothership_bullet, &craft_sprite)){
		materialise_spaceship();
		mothership_bullet.is_visible = 0;
		lives--;
	}

	if(has_collided_sprite(&mothership_sprite, &craft_sprite)){
		send_debug_string("Mothership destroyed player.");
		materialise_spaceship();
		lives--;
	}

	for(int i = 0; i < BULLET_COUNT; i++){
		if(has_collided_sprite(&bullet_sprite[i], &mothership_sprite)){
			mothership_lives--;
			bullet_sprite[i].is_visible = 0;
		}
	}

	for(int i = 0; i < ALIEN_COUNT; i++){
		if(has_collided_sprite(&craft_sprite, &alien_sprite[i])){
			send_debug_string("Alien destroyed player.");
			materialise_spaceship();
			lives--;
		}
	}

}

void mothership_attack(){
	double angle = angle_to(mothership_sprite.x, mothership_sprite.y, craft_sprite.x, craft_sprite.y);
	mothership_sprite.dx = cos(angle) * MOTHERSHIP_SPEED;
	mothership_sprite.dy = sin(angle) * MOTHERSHIP_SPEED;
}

void alien_attack(int alien){
	double angle = angle_to(alien_sprite[alien].x, alien_sprite[alien].y, craft_sprite.x, craft_sprite.y);
	alien_sprite[alien].dx = cos(angle) * ALIEN_SPEED;
	alien_sprite[alien].dy = sin(angle) * ALIEN_SPEED;
}

void check_alien_wall(){
	for(int i = 0; i < ALIEN_COUNT; i++){
		if ((alien_sprite[i].x <= 2 || alien_sprite[i].x >= LCD_X - 6 || alien_sprite[i].y <= 11 || alien_sprite[i].y >= LCD_Y - 6) /*&& !on_wall[i]*/){
			on_wall[i] = 1;
			alien_sprite[i].dx = 0;
			alien_sprite[i].dy = 0;
			if(alien_wait[i] <= -5){
				alien_wait[i] = -((double)((rand() % 200) + 200)/100.0);
				//sprintf(buff, "Alien %d: %f", i, alien_wait[i]);
				//send_line(buff);
			}
		}
	}
	if ((mothership_sprite.x <= 2 || mothership_sprite.x >= LCD_X - 11 || mothership_sprite.y <= 11 || mothership_sprite.y >= LCD_Y - 9) /*&& !on_wall[i]*/){
		m_on_wall = 1;
		mothership_sprite.dx = 0;
		mothership_sprite.dy = 0;
		if(mothership_wait <= -5){
			mothership_wait = -((double)((rand() % 200) + 200)/100.0);
		}
	}
}

void shoot(int degrees){

	double radians = degrees * M_PI / 180;

	for(int i = 0; i < BULLET_COUNT; i++){
		if (!bullet_sprite[i].is_visible){
			bullet_sprite[i].is_visible = 1;
			bullet_sprite[i].x = aim_x;
			bullet_sprite[i].y = aim_y;
			bullet_sprite[i].dx = cos(radians) * BULLET_SPEED;
			bullet_sprite[i].dy = sin(radians) * BULLET_SPEED;
			break;
		}
	}
}

double angle_to(double x1, double y1, double x2, double y2){
	return atan2((y2 - y1), (x2 - x1));
}

void step_sprites(){

	for (int i = 0; i < ALIEN_COUNT; i++){
		if(alien_sprite[i].is_visible){
			sprite_step(&alien_sprite[i]);
			if(alien_sprite[i].x > 1 && alien_sprite[i].x < LCD_X - 6 && alien_sprite[i].y > 11 && alien_sprite[i].y < LCD_Y - 6) on_wall[i] = 0;
		}
	}
	for (int i = 0; i < BULLET_COUNT; i++){
		if(bullet_sprite[i].is_visible){
			sprite_step(&bullet_sprite[i]);

			if(bullet_sprite[i].x > LCD_X - 1 || bullet_sprite[i].x < 1 || bullet_sprite[i].y > LCD_Y - 1 
				|| bullet_sprite[i].y < 10) {
				bullet_sprite[i].is_visible = 0;
			}
		}
	}

	if(mothership_sprite.is_visible){
		sprite_step(&mothership_sprite);
		if(mothership_sprite.x > 1 && mothership_sprite.x < LCD_X - 6 && mothership_sprite.y > 11 && mothership_sprite.y < LCD_Y - 6) m_on_wall = 0;
	}
	if(mothership_bullet.is_visible){
		sprite_step(&mothership_bullet);
		if(mothership_bullet.x > LCD_X - 1 || mothership_bullet.x < 1 || mothership_bullet.y > LCD_Y - 1 
				|| mothership_bullet.y < 10) {
				mothership_bullet.is_visible = 0;
			}
	}
}

void draw_sprites(){
	for (int i = 0; i < ALIEN_COUNT; i++){
		if(alien_sprite[i].is_visible){
			draw_sprite(&alien_sprite[i]);
		}
	}
	for (int i = 0; i < BULLET_COUNT; i++){
		if(bullet_sprite[i].is_visible){
			draw_sprite(&bullet_sprite[i]);
		}
	}
	if(mothership_sprite.is_visible){
		draw_sprite(&mothership_sprite);
		draw_boss_health();
	}
	if(mothership_bullet.is_visible){
		draw_sprite(&mothership_bullet);
	}
}

void gameLoop(){

	init_variables();
	_delay_ms(500);
	intro_menu();
	srand(TCNT1);
	init_sprites();

	materialise_spaceship();
	materialise_aliens();

	TCNT0 = 0;
	overflow_count = 0;
	gameRunning = 1;

	while (gameRunning){
		process_time();
		ADC_prep();
		clear_screen();
		draw_status_border();
		process_input();
		//sprite_step(&craft_sprite);
		draw_sprite(&craft_sprite);
		//sprite_step(&alien_sprite);
		//draw_sprite(&alien_sprite);
		step_sprites();
		draw_sprites();
		check_alien_wall();
		check_collision();
		draw_aim_line(ceil(ADC * 0.705));
		//previous_time = get_system_time();
		show_screen();

		if(lives < 1) { gameRunning = 0; break; };
		if(mothership_lives < 1){
			mothership_fire = -5;
			mothership_sprite.is_visible = 0;
			mothership_lives = 10;
			score += 10;
			send_debug_string("Player destroyed mothership.");

			materialise_aliens();
			boss_time = 1;
		}
		if(aliens_dead() && boss_time) { boss_battle(); boss_time = 0; }
	}
	clear_screen();
	show_screen();
}

void playagain(){

	clear_screen();
	draw_string((LCD_X - (9*5))/2, 0, "GAME OVER");
	draw_string(1, 16, "You have lost");
	draw_string(1, 24, "Alien Advance");
	draw_string(1, 32, "Press a button");
	draw_string(1, 40, "to restart...");
	show_screen();
	wait_for_press();

}

int main(){

	

	set_clock_speed(CPU_8MHz);
	init_hardware();

	draw_centred(17, "Waiting for");
	draw_centred(24, "USB Connection...");

	show_screen();
	while(!usb_configured() || !usb_serial_get_control());
	send_debug_string("Greetings from the teensy. Debugger initialised.");
	clear_screen();
	draw_centred(17, "Connected to");
	draw_centred(24, "USB!");
	show_screen();
	_delay_ms(500);

	while(1){
		gameLoop();
		playagain();
	}
	return 0;
}

/*
* Interrupt service routines
*/

ISR(TIMER4_OVF_vect){
	difference = get_global_time() - previous_time;
	previous_time = get_global_time();

	for (int i = 0; i < NUM_BUTTONS; i++){
		btn_hists[i] = btn_hists[i]<<1;
	}

	if (PIND >> PD1 & 1) btn_hists[BTN_DPAD_UP] |= 1;
	if (PIND >> PD0 & 1) btn_hists[BTN_DPAD_RIGHT] |= 1; 
	if (PINB >> PB1 & 1) btn_hists[BTN_DPAD_LEFT] |= 1;
	if (PINB >> PB7 & 1) btn_hists[BTN_DPAD_DOWN] |= 1;
	if (PINF >> PF5 & 1) btn_hists[BTN_RIGHT] |= 1;
	if (PINF >> PF6 & 1) btn_hists[BTN_LEFT] |= 1;

	for (int i = 0; i < NUM_BUTTONS; i++){
		if(btn_hists[i] == 0xFF && btn_held[i] == BTN_STATE_UP){
			btn_held[i] = BTN_STATE_DOWN;
		}
		else if (btn_hists[i] == 0 && btn_held[i] == BTN_STATE_DOWN){
			btn_held[i] = BTN_STATE_UP;
			if(i == BTN_LEFT || i == BTN_RIGHT){
				shoot(ceil(ADC * 0.705));
			}
		}
	}

	for(int i = 0; i < ALIEN_COUNT; i++){
		

		if(alien_wait[i] >= 0){
			alien_wait[i] = -5;
			alien_attack(i);
		}
		else if (alien_wait[i] < -4){

		}
		else{
			alien_wait[i] += difference;
		}
		
	}

	if(mothership_wait >= 0){
		mothership_wait = -5;
		mothership_attack();
	}
	else if(mothership_wait < -4){

	}
	else {
		mothership_wait += difference;
	}

	if(mothership_sprite.is_visible){
		if(mothership_fire >= 0){
		boss_shoot();
		mothership_fire = -((double)((rand() % 200) + 200)/100.0);
		}
		else if(mothership_fire < -4){

		}
		else {
			mothership_fire += difference;
		}
	}
}


ISR(TIMER3_COMPA_vect) {
	if(usb_configured() && usb_serial_get_control() && gameRunning){
		send_status();
	}
}

ISR(TIMER1_OVF_vect) {
	debug_overflow_count++;
	
}

ISR(TIMER0_OVF_vect) {
	overflow_count++;
}

