#define KB_KUP  0
#define KB_SHIFT  1
#define KB_CTRL  2
#define KB_ALT  3
#define KB_CAPSLK 4
#define KB_NUMLK 5
#define KB_SCRLK 6
#define KB_EXT 7


#ifndef F_CPU
#define F_CPU 16000000UL
#endif

const uint8_t ps2_to_ascii[] = // Scancode > Ascii table.
{
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '`', 0, // 00-0F
	0, 0, 0, 0, 0, 'q', '1', 0, 0, 0, 'z', 's', 'a', 'w', '2', 0, //10-1F 
	0, 'c', 'x', 'd', 'e', '4', '3', 0, 0, ' ', 'v', 'f', 't', 'r', '5', 0, //20-2F
	0, 'n', 'b', 'h', 'g', 'y', '6', 0, 0, 0, 'm', 'j', 'u', '7', '8', 0, //30-3F
	0, ',', 'k', 'i', 'o', '0', '9', 0, 0, '.', '/', 'l', ';', 'p', '-', 0, //40-4F
	0, 0, '\'', 0, '[', '=', 0, 0, 0, 0, 0, ']', 0, '\\', 0, 0, //50-5F
	0, 0, 0, 0, 0, 0, 0, 0, 0, '1', 0, '4', '7', 0, 0, 0, //60-6F
	'0', '.', '2', '5', '6', '8', 0, 0, 0, '+', '3', '-', '*', '9', 0, 0, //70-7F
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 //80-8F
};

const uint8_t ps2_to_ascii_shifted[] = 
{
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '~', 0, // 00-0F
	0, 0, 0, 0, 0, 'Q', '!', 0, 0, 0, 'Z', 'S', 'A', 'W', '@', 0, //10-1F 
	0, 'C', 'X', 'D', 'E', '$', '#', 0, 0, ' ', 'V', 'F', 'T', 'R', '%', 0, //20-2F
	0, 'N', 'B', 'H', 'G', 'Y', '^', 0, 0, 0, 'M', 'J', 'U', '&', '*', 0, //30-3F
	0, '<', 'K', 'I', 'O', ')', '(', 0, 0, '>', '?', 'L', ':', 'P', '_', 0, //40-4F
	0, 0, '"', 0, '{', '+', 0, 0, 0, 0, 0, '}', 0, '|', 0, 0, //50-5F
	0, 0, 0, 0, 0, 0, 0, 0, 0, '1', 0, '4', '7', 0, 0, 0, //60-6F
	'0', '.', '2', '5', '6', '8', 0, 0, 0, '+', '3', '-', '*', '9', 0, 0, //70-7F
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 //80-8F
};
	