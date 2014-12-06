// vim: set foldmethod=marker :
#ifndef _ARCH_AVR_H
// Includes and defines. {{{
//#define DEBUG_AVRCOMM

#define _ARCH_AVR_H
#include <stdint.h>
#include <unistd.h>
#include <termios.h>
#include <cstdlib>
#include <cstdio>
#include <sys/time.h>
#include <poll.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <sys/mman.h>

#define ADCBITS 10
#define debug(...) do { buffered_debug_flush(); fprintf(stderr, "#"); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while (0)
#define F(x) (x)
#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))

// Not defines, because they can change value.
EXTERN uint8_t NUM_DIGITAL_PINS, NUM_ANALOG_INPUTS, NUM_MOTORS, NUM_BUFFERS, FRAGMENTS_PER_BUFFER, BYTES_PER_FRAGMENT;
// }}}

enum Control {
	CTRL_RESET,
	CTRL_SET,
	CTRL_UNSET,
	CTRL_INPUT
};

enum HWCommands { // {{{
	HWC_BEGIN = 0x40,
	HWC_PING,	// 41
	HWC_RESET,	// 42
	HWC_SETUP,	// 43
	HWC_CONTROL,	// 44
	HWC_MSETUP,	// 45
	HWC_ASETUP,	// 46
	HWC_START_MOVE,	// 47
	HWC_MOVE,	// 48
	HWC_START,	// 49
	HWC_STOP,	// 4a
	HWC_ABORT,	// 4b
	HWC_GETPIN,	// 4c

	HWC_READY = 0x60,
	HWC_PONG,	// 61
	HWC_PIN,	// 62
	HWC_STOPPED,	// 63

	HWC_DONE,	// 64
	HWC_UNDERRUN,	// 65
	HWC_ADC,	// 66
	HWC_LIMIT,	// 67
	HWC_SENSE0,	// 68
	HWC_SENSE1	// 69
};
// }}}

extern int avr_active_motors;
static inline int hwpacketsize(int len, int available) {
	int const arch_packetsize[16] = { 0, 2, 2, 0, 2, 2, 4, 0, 0, 0, -1, -1, -1, -1, -1, -1 };
	switch (command[1][0] & ~0x10) {
	case HWC_BEGIN:
		if (len >= 2)
			return command[1][1];
		if (available == 0)
			return 2;	// The data is not available, so this will not trigger the packet to be parsed yet.
		command[1][1] = serialdev[1]->read();
		command_end[1] += 1;
		return command[1][1];
	case HWC_STOPPED:
	case HWC_LIMIT:
		return 3 + 4 * avr_active_motors;
	case HWC_SENSE0:
	case HWC_SENSE1:
		return 2 + 4 * avr_active_motors;
	default:
		return arch_packetsize[command[1][0] & 0xf];
	}
}

struct AVRSerial : public Serial_t { // {{{
	char buffer[256];
	int start, end, fd;
	inline void begin(char const *port, int baud);
	inline void write(char c);
	inline void refill();
	inline int read();
	int readBytes (char *target, int len) {
		for (int i = 0; i < len; ++i)
			*target++ = read();
		return len;
	}
	void flush() {}
	int available() {
		if (start == end)
			refill();
		return end - start;
	}
};
// }}}
struct HostSerial : public Serial_t { // {{{
	char buffer[256];
	int start, end;
	inline void begin(int baud);
	inline void write(char c);
	inline void refill();
	inline int read();
	int readBytes (char *target, int len) {
		for (int i = 0; i < len; ++i)
			*target++ = read();
		return len;
	}
	void flush() {}
	int available() {
		if (start == end)
			refill();
		return end - start;
	}
};
// }}}

// Declarations of static variables; extern because this is a header file. {{{
EXTERN HostSerial host_serial;
EXTERN AVRSerial avr_serial;
EXTERN bool avr_wait_for_reply;
EXTERN uint8_t avr_pong;
EXTERN char avr_buffer[256];
EXTERN int avr_limiter_space;
EXTERN int avr_limiter_motor;
EXTERN int *avr_adc;
EXTERN bool avr_running;
EXTERN char *avr_pins;
EXTERN int32_t *avr_pos_offset;
EXTERN char ***avr_buffers;
EXTERN int avr_active_motors;
EXTERN int *avr_adc_id;
// }}}

// Time handling.  {{{
static inline void get_current_times(uint32_t *current_time, uint32_t *longtime) {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	if (current_time)
		*current_time = tv.tv_sec * 1000000 + tv.tv_usec;
	if (longtime)
		*longtime = tv.tv_sec * 1000 + tv.tv_usec / 1000;
	//fprintf(stderr, "current times: %d %d\n", *current_time, *longtime);
}

static inline uint32_t utime() {
	uint32_t ret;
	get_current_times(&ret, NULL);
	return ret;
}
static inline uint32_t millis() {
	uint32_t ret;
	get_current_times(NULL, &ret);
	return ret;
}
// }}}

static inline void avr_write_ack();

// Serial port communication. {{{
static inline void avr_send() {
	//debug("avr_send");
	send_packet();
	for (int counter = 0; counter < 0x20 && out_busy; ++counter) {
		//debug("avr send");
		poll(&pollfds[1], 1, 100);
		serial(1);
		//if (out_busy[1])
		//	debug("avr waiting for ack");
		if (out_busy && (counter & 0x7) == 0x7) {
			debug("resending packet");
			send_packet();
		}
	}
	if (out_busy) {
		debug("avr_send failed, packet start: %02x %02x %02x", avr_buffer[0], avr_buffer[1], avr_buffer[2]);
		out_busy = false;
	}
}

static inline void avr_call1(uint8_t cmd, uint8_t arg) {
	avr_buffer[0] = cmd;
	avr_buffer[1] = arg;
	prepare_packet(avr_buffer, 2);
	avr_send();
}

static inline void avr_get_reply() {
	for (int counter = 0; avr_wait_for_reply && counter < 0x10; ++counter) {
		//debug("avr wait");
		pollfds[1].revents = 0;
		poll(&pollfds[1], 1, 100);
		serial(1);
		if (avr_wait_for_reply && (counter & 0x7) == 0x7) {
			debug("no reply; resending");
			avr_send();
		}
	}
}

static inline void hwpacket(int len) {
	// Handle data in command[1].
#if 0
	fprintf(stderr, "packet received:");
	for (uint8_t i = 0; i < len; ++i)
		fprintf(stderr, " %02x", command[1][i]);
	fprintf(stderr, "\n");
#endif
	switch (command[1][0] & ~0x10) {
	case HWC_LIMIT:
	case HWC_SENSE0:
	case HWC_SENSE1:
	{
		uint8_t which = command[1][1];
		if (which >= NUM_MOTORS) {
			debug("cdriver: Invalid limit or sense for avr motor %d", which);
			write_stall();
			return;
		}
		avr_write_ack();
		int s = 0, m = 0;
		for (s = 0; s < num_spaces; ++s) {
			if (which < spaces[s].num_motors) {
				m = which;
				break;
			}
			which -= spaces[s].num_motors;
		}
		//debug("cp1 %d", spaces[s].motor[m]->current_pos);
		int offset;
		if ((command[1][0] & ~0x10) == HWC_LIMIT) {
			debug("limit!");
			offset = 3;
		}
		else
			offset = 2;
		int mi = 0;
		for (int ts = 0; ts < num_spaces; mi += spaces[ts++].num_motors) {
			for (int tm = 0; tm < spaces[ts].num_motors; ++tm) {
				spaces[ts].motor[tm]->current_pos = -avr_pos_offset[tm + mi];
				for (int i = 0; i < 4; ++i) {
					spaces[ts].motor[tm]->current_pos += int(uint8_t(command[1][offset + 4 * (tm + mi) + i])) << (i * 8);
				}
				spaces[ts].motor[tm]->hwcurrent_pos = spaces[ts].motor[tm]->current_pos;
				debug("current pos %d %d %d %d %d", ts, tm, spaces[ts].motor[tm]->current_pos, avr_pos_offset[tm + mi], spaces[ts].motor[tm]->current_pos + avr_pos_offset[tm + mi]);
			}
		}
		if ((command[1][0] & ~0x10) == HWC_LIMIT) {
			debug("limit!");
			abort_move(false);
			stopping = 2;
			send_host(CMD_LIMIT, s, m, spaces[s].motor[m]->current_pos / spaces[s].motor[m]->steps_per_m, cbs_after_current_move);
			cbs_after_current_move = 0;
			avr_running = false;
			free_fragments = FRAGMENTS_PER_BUFFER;
			// Ignore time.
		}
		else {
			spaces[s].motor[m]->sense_state = 1;
			send_host(CMD_SENSE, s, m, 0, (command[1][1] & ~0x10) == HWC_SENSE1);
		}
		return;
	}
	case HWC_PONG:
	{
		avr_pong = command[1][1];
		avr_write_ack();
		return;
	}
	case HWC_ADC:
	{
		int pin = command[1][1];
		if (pin < 0 || pin >= NUM_ANALOG_INPUTS)
			debug("invalid adc %d received", pin);
		else {
			avr_adc[pin] = (command[1][2] & 0xff) | ((command[1][3] & 0xff) << 8);
			//debug("adc %d = %d", pin, avr_adc[pin]);
		}
		avr_write_ack();
		if (avr_adc_id[pin] != ~0)
			handle_temp(avr_adc_id[pin], avr_adc[pin]);
		return;
	}
	case HWC_UNDERRUN:
	{
		debug("underrun");
		avr_running = false;
		// Fall through.
	}
	case HWC_DONE:
	{
		free_fragments += command[1][1];
		avr_write_ack();
		buffer_refill();
		return;
	}
	default:
	{
		if (!avr_wait_for_reply)
			debug("received unexpected reply!");
		avr_wait_for_reply = false;
		return;
	}
	}
}

static inline void avr_write_ack() {
	write_ack();
}

static inline bool arch_active() {
	return true;
}
// }}}

// Hardware interface {{{
static inline void avr_setup_pin(int pin, int type) {
	avr_buffer[0] = HWC_CONTROL;
	avr_buffer[1] = 1;
	avr_buffer[2] = 0xc | type;
	avr_buffer[3] = pin;
	prepare_packet(avr_buffer, 4);
	avr_send();
}

static inline void SET_INPUT(Pin_t _pin) {
	if (!_pin.valid())
		return;
	if (avr_pins[_pin.pin] == 2)
		return;
	avr_pins[_pin.pin] = 2;
	avr_setup_pin(_pin.pin, CTRL_INPUT);
}

static inline void SET_INPUT_NOPULLUP(Pin_t _pin) {
	if (!_pin.valid())
		return;
	if (avr_pins[_pin.pin] == 3)
		return;
	avr_pins[_pin.pin] = 3;
	avr_setup_pin(_pin.pin, CTRL_UNSET);
}

static inline void RESET(Pin_t _pin) {
	if (!_pin.valid())
		return;
	if (avr_pins[_pin.pin] == 0)
		return;
	avr_pins[_pin.pin] = 0;
	avr_setup_pin(_pin.pin, _pin.inverted() ? CTRL_SET : CTRL_RESET);
}

static inline void SET(Pin_t _pin) {
	if (!_pin.valid())
		return;
	if (avr_pins[_pin.pin] == 1)
		return;
	avr_pins[_pin.pin] = 1;
	avr_setup_pin(_pin.pin, _pin.inverted() ? CTRL_RESET : CTRL_SET);
}

static inline void SET_OUTPUT(Pin_t _pin) {
	if (!_pin.valid())
		return;
	if (avr_pins[_pin.pin] < 2)
		return;
	RESET(_pin);
}

static inline bool GET(Pin_t _pin, bool _default) {
	if (!_pin.valid())
		return _default;
	if (avr_wait_for_reply)
		debug("cdriver problem: avr_wait_for_reply already set!");
	avr_wait_for_reply = true;
	avr_call1(HWC_GETPIN, _pin.pin);
	avr_get_reply();
	avr_write_ack();
	return _pin.inverted() ^ command[1][2];
}
// }}}

// Setup hooks. {{{
static inline void arch_reset() {
	// Initialize connection.
	if (avr_pong == 2) {
		debug("reset ignored");
		return;
	}
	avr_serial.write(CMD_ACK0);
	avr_serial.write(CMD_ACK1);
	avr_call1(HWC_PING, 0);
	avr_call1(HWC_PING, 1);
	avr_call1(HWC_PING, 2);
	for (int counter = 0; avr_pong != 2 && counter < 100; ++counter) {
		//debug("avr pongwait %d", avr_pong);
		pollfds[1].revents = 0;
		poll(&pollfds[1], 1, 10);
		serial(1);
	}
	if (avr_pong != 2) {
		debug("no pong seen; giving up.\n");
		reset();
	}
}

enum MotorFlags {
	LIMIT = 1,
	SENSE0 = 2,
	SENSE1 = 4,
	INVERT_LIMIT_MIN = 8,
	INVERT_LIMIT_MAX = 16,
	INVERT_STEP = 32,
	SENSE_STATE = 64,
	ACTIVE = 128
};

static inline void arch_motor_change(uint8_t s, uint8_t sm) {
	uint8_t m = sm;
	for (uint8_t st = 0; st < s; ++st)
		m += spaces[st].num_motors;
	avr_buffer[0] = HWC_MSETUP;
	avr_buffer[1] = m;
	Motor &mtr = *spaces[s].motor[sm];
	//debug("arch motor change %d %d %d %x", s, sm, m, p);
	avr_buffer[2] = (mtr.step_pin.valid() ? mtr.step_pin.pin : ~0);
	avr_buffer[3] = (mtr.dir_pin.valid() ? mtr.dir_pin.pin : ~0);
	if (mtr.dir_pin.inverted()) {
		avr_buffer[4] = (mtr.limit_max_pin.valid() ? mtr.limit_max_pin.pin : ~0);
		avr_buffer[5] = (mtr.limit_min_pin.valid() ? mtr.limit_min_pin.pin : ~0);
	}
	else {
		avr_buffer[4] = (mtr.limit_min_pin.valid() ? mtr.limit_min_pin.pin : ~0);
		avr_buffer[5] = (mtr.limit_max_pin.valid() ? mtr.limit_max_pin.pin : ~0);
	}
	avr_buffer[6] = (mtr.sense_pin.valid() ? mtr.sense_pin.pin : ~0);
	avr_buffer[7] = ACTIVE | (mtr.step_pin.inverted() ? INVERT_STEP : 0) | (mtr.limit_min_pin.inverted() ? INVERT_LIMIT_MIN : 0) | (mtr.limit_max_pin.inverted() ? INVERT_LIMIT_MAX : 0);
	prepare_packet(avr_buffer, 8);
	avr_send();
}

static inline void arch_change(bool motors) {
	avr_buffer[0] = HWC_SETUP;
	avr_buffer[1] = led_pin.valid() ? led_pin.pin : ~0;
	for (int i = 0; i < 4; ++i)
		avr_buffer[2 + i] = (hwtime_step >> (8 * i)) & 0xff;
	prepare_packet(avr_buffer, 6);
	avr_send();
	if (motors) {
		int num_motors = 0;
		for (uint8_t s = 0; s < num_spaces; ++s) {
			for (uint8_t m = 0; m < spaces[s].num_motors; ++m) {
				arch_motor_change(s, m);
				num_motors += 1;
			}
		}
		for (int m = num_motors; m < avr_active_motors; ++m) {
			avr_buffer[0] = HWC_MSETUP;
			avr_buffer[1] = m;
			//debug("arch motor change %d %d %d %x", s, sm, m, p);
			avr_buffer[2] = ~0;
			avr_buffer[3] = ~0;
			avr_buffer[4] = ~0;
			avr_buffer[5] = ~0;
			avr_buffer[6] = ~0;
			avr_buffer[7] = 0;
			prepare_packet(avr_buffer, 8);
			avr_send();
		}
		avr_active_motors = num_motors;
	}
}

static inline void arch_motors_change() {
	arch_change(true);
}

static inline void arch_globals_change() {
	arch_change(false);
}

static inline void arch_setup_start(char const *port) {
	// Set up arch variables.
	avr_wait_for_reply = false;
	avr_adc = NULL;
	avr_buffers = NULL;
	avr_running = false;
	NUM_ANALOG_INPUTS = 0;
	avr_pong = ~0;
	avr_limiter_space = -1;
	avr_limiter_motor = 0;
	avr_active_motors = 0;
	// Set up serial port.
	avr_serial.begin(port, 115200);
	serialdev[1] = &avr_serial;
	arch_reset();
}

static inline void arch_setup_end() {
	// Get constants.
	avr_buffer[0] = HWC_BEGIN;
	if (avr_wait_for_reply)
		debug("avr_wait_for_reply already set in begin");
	avr_wait_for_reply = true;
	prepare_packet(avr_buffer, 1);
	avr_send();
	avr_get_reply();
	avr_write_ack();
	protocol_version = 0;
	for (uint8_t i = 0; i < sizeof(uint32_t); ++i)
		protocol_version |= int(uint8_t(command[1][2 + i])) << (i * 8);
	NUM_DIGITAL_PINS = command[1][6];
	NUM_ANALOG_INPUTS = command[1][7];
	NUM_MOTORS = command[1][8];
	NUM_BUFFERS = command[1][9];
	FRAGMENTS_PER_BUFFER = command[1][10];
	BYTES_PER_FRAGMENT = command[1][11];
	free_fragments = FRAGMENTS_PER_BUFFER;
	fragment_len = new int[FRAGMENTS_PER_BUFFER];
	num_active_motors = new int[FRAGMENTS_PER_BUFFER];
	avr_pong = -1;	// Choke on reset again.
	avr_pins = new char[NUM_DIGITAL_PINS];
	for (int i = 0; i < NUM_DIGITAL_PINS; ++i)
		avr_pins[i] = 3;	// INPUT_NOPULLUP.
	avr_adc = new int[NUM_ANALOG_INPUTS];
	avr_adc_id = new int[NUM_ANALOG_INPUTS];
	for (int i = 0; i < NUM_ANALOG_INPUTS; ++i) {
		avr_adc[i] = ~0;
		avr_adc_id[i] = ~0;
	}
	avr_buffers = new char **[NUM_BUFFERS];
	for (int b = 0; b < NUM_BUFFERS; ++b) {
		avr_buffers[b] = new char *[FRAGMENTS_PER_BUFFER];
		for (int f = 0; f < FRAGMENTS_PER_BUFFER; ++f) {
			avr_buffers[b][f] = new char[BYTES_PER_FRAGMENT];
			memset(avr_buffers[b][f], 0, BYTES_PER_FRAGMENT);
		}
	}
	avr_pos_offset = new int32_t[NUM_MOTORS];
	for (int m = 0; m < NUM_MOTORS; ++m)
		avr_pos_offset[m] = 0;
	reset_dirs(0);
}

static inline void arch_setup_temp(int id, int thermistor_pin, bool active, int power_pin = ~0, bool invert = false, int adctemp = 0) {
	avr_adc_id[thermistor_pin] = id;
	adctemp &= 0x3fff;
	avr_buffer[0] = HWC_ASETUP;
	avr_buffer[1] = thermistor_pin;
	avr_buffer[2] = power_pin;
	avr_buffer[3] = ~0;
	avr_buffer[4] = adctemp & 0xff;
	avr_buffer[5] = (adctemp >> 8) & 0xff | (invert ? 0x40 : 0) | (active ? 0 : 0x80);
	avr_buffer[6] = ~0;
	avr_buffer[7] = ~0;
	prepare_packet(avr_buffer, 8);
	avr_send();
}
// }}}

// Running hooks. {{{
static inline void arch_addpos(uint8_t s, uint8_t m, int diff) {
	for (uint8_t st = 0; st < s; ++st)
		m += spaces[st].num_motors;
	//debug("setpos %d %d", m, pos.i);
	avr_pos_offset[m] -= diff;
}

static inline void arch_clear_fragment() {
	for (int b = 0; b < NUM_BUFFERS; ++b)
		memset(avr_buffers[b][current_fragment], 0, BYTES_PER_FRAGMENT);
}

static inline void arch_send_fragment(int fragment) {
	if (stopping)
		return;
	avr_buffer[0] = HWC_START_MOVE;
	//debug("send fragment %d %d", fragment_len[fragment], fragment);
	avr_buffer[1] = fragment_len[fragment];
	avr_buffer[2] = num_active_motors[fragment];
	prepare_packet(avr_buffer, 3);
	avr_send();
	int mi = 0;
	for (int s = 0; !stopping && s < num_spaces; mi += spaces[s++].num_motors) {
		if (!spaces[s].active)
			continue;
		for (uint8_t m = 0; !stopping && m < spaces[s].num_motors; ++m) {
			if (spaces[s].motor[m]->dir[fragment] == 0)
				continue;
			//debug("sending %d %d %d", s, m, spaces[s].motor[m]->dir[fragment]);
			avr_buffer[0] = HWC_MOVE;
			avr_buffer[1] = mi + m;
			avr_buffer[2] = ((spaces[s].motor[m]->dir[fragment] < 0) ^ (spaces[s].motor[m]->dir_pin.inverted()) ? 1 : 0);
			int bytes = (fragment_len[fragment] + 1) / 2;
			for (int i = 0; i < bytes; ++i) {
				avr_buffer[3 + i] = avr_buffers[m + mi][fragment][i];
			}
			prepare_packet(avr_buffer, 3 + bytes);
			avr_send();
		}
	}
	fragment = (fragment + 1) % FRAGMENTS_PER_BUFFER;
}

static inline void arch_set_value(int m, int value) {
	avr_buffers[m][current_fragment][current_fragment_pos >> 1] |= value << (4 * (current_fragment_pos & 0x1));
}

static inline void arch_start_move() {
	if (avr_running)
		return;
	avr_running = true;
	avr_buffer[0] = HWC_START;
	prepare_packet(avr_buffer, 1);
	avr_send();
}
// }}}

// ADC hooks. {{{
static inline int adc_get(uint8_t _pin) {
	if (_pin >= NUM_ANALOG_INPUTS) {
		debug("request for invalid adc input %d", _pin);
		return ~0;
	}
	int ret = avr_adc[_pin];
	avr_adc[_pin] = ~0;
	return ret;
}

static inline bool adc_ready(uint8_t _pin) {
	if (_pin >= NUM_ANALOG_INPUTS) {
		debug("request for state of invalid adc input %d", _pin);
		return false;
	}
	return ~avr_adc[_pin];
}
// }}}

// Debugging hooks. {{{
static inline void START_DEBUG() {
	fprintf(stderr, "cdriver debug from firmware: ");
}

static inline void DO_DEBUG(char c) {
	fprintf(stderr, "%c"
#ifdef DEBUG_AVRCOMM
				" "
#endif
				, c);
}

static inline void END_DEBUG() {
	fprintf(stderr, "\n");
}
// }}}

// Inline AVRSerial methods. {{{
void AVRSerial::begin(char const *port, int baud) {
	// Open serial port and prepare pollfd.
	fd = open(port, O_RDWR);
	pollfds[1].fd = fd;
	pollfds[1].events = POLLIN | POLLPRI;
	pollfds[1].revents = 0;
	start = 0;
	end = 0;
	fcntl(fd, F_SETFL, O_NONBLOCK);
#if 0
	// Set baud rate and control.
	tcflush(fd, TCIOFLUSH);
	struct termios opts;
	tcgetattr(fd, &opts);
	opts.c_iflag = IGNBRK;
	opts.c_oflag = 0;
	opts.c_cflag = CS8 | CREAD | CLOCAL;
	opts.c_lflag = 0;
	opts.c_cc[VMIN] = 1;
	opts.c_cc[VTIME] = 0;
	cfsetispeed(&opts, B115200);
	cfsetospeed(&opts, B115200);
	tcsetattr(fd, TCSANOW, &opts);
#endif
}

void AVRSerial::write(char c) {
#ifdef DEBUG_AVRCOMM
	debug("w\t%02x", c & 0xff);
#endif
	while (true) {
		errno = 0;
		int ret = ::write(fd, &c, 1);
		if (ret == 1)
			break;
		if (errno != EAGAIN && errno != EWOULDBLOCK) {
			debug("write to avr failed: %d %s", ret, strerror(errno));
			abort();
		}
	}
}

void AVRSerial::refill() {
	start = 0;
	end = ::read(fd, buffer, sizeof(buffer));
	//debug("refill %d bytes", end);
	if (end < 0) {
		if (errno != EAGAIN && errno != EWOULDBLOCK)
			debug("read returned error: %s", strerror(errno));
		end = 0;
	}
	if (end == 0 && pollfds[1].revents) {
		debug("EOF detected on serial port; exiting.");
		reset();
	}
	pollfds[1].revents = 0;
}

int AVRSerial::read() {
	if (start == end)
		refill();
	if (start == end) {
		debug("eof on input; exiting.");
		reset();
	}
	int ret = buffer[start++];
#ifdef DEBUG_AVRCOMM
	debug("r %02x", ret & 0xff);
#endif
	return ret;
}
// }}}

// Inline HostSerial methods. {{{
void HostSerial::begin(int baud) {
	pollfds[0].fd = 0;
	pollfds[0].events = POLLIN | POLLPRI;
	pollfds[0].revents = 0;
	start = 0;
	end = 0;
	fcntl(0, F_SETFL, O_NONBLOCK);
}

void HostSerial::write(char c) {
	//debug("Firmware write byte: %x", c);
	while (true) {
		errno = 0;
		int ret = ::write(1, &c, 1);
		if (ret == 1)
			break;
		if (errno != EAGAIN && errno != EWOULDBLOCK) {
			debug("write to host failed: %d %s", ret, strerror(errno));
			exit(0);
		}
	}
}

void HostSerial::refill() {
	start = 0;
	end = ::read(0, buffer, sizeof(buffer));
	//debug("refill %d bytes", end);
	if (end < 0) {
		if (errno != EAGAIN && errno != EWOULDBLOCK)
			debug("read returned error: %s", strerror(errno));
		end = 0;
	}
	if (end == 0 && pollfds[0].revents) {
		debug("EOF detected on standard input; exiting.");
		reset();
	}
	pollfds[0].revents = 0;
}

int HostSerial::read() {
	if (start == end)
		refill();
	if (start == end) {
		debug("eof on input; exiting.");
		reset();
	}
	int ret = buffer[start++];
	//debug("Firmware read byte: %x", ret);
	return ret;
}
// }}}

#endif
