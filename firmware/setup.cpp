#include "firmware.h"

void setup()
{
	arch_setup_start();
	watchdog_disable();
	printer_type = ~0;
	setup_spacetypes();
	// Initialize volatile variables.
	Serial.begin(115200);
	initialized = false;
	debug_buffer_ptr = 0;
	adc_phase = 0;
#ifdef HAVE_TEMPS
	temp_current = 0;
#endif
	command_end = 0;
	motors_busy = false;
	queue_start = 0;
	queue_end = 0;
	queue_full = false;
	num_movecbs = 0;
	continue_cb = 0;
	ping = 0;
	last_packet = NULL;
	out_busy = false;
	reply_ready = false;
	led_pin.read(0);
	probe_pin.read(0);
	led_phase = 0;
	temps_busy = 0;
	requested_temp = ~0;
	led_last = millis();
	last_active = millis();
	t0 = 0;
	f0 = 0;
	//debug("moving->false");
	moving = false;
	move_prepared = false;
	current_move_has_cb = false;
	which_autosleep = 0;
#ifdef HAVE_AUDIO
	audio_head = 0;
	audio_tail = 0;
	audio_state = 0;
	audio_us_per_bit = 125; // 1000000 / 8000;
#endif
	for (uint8_t i = 0; i < ID_SIZE; ++i)
		printerid[i] = 0;
#ifdef HAVE_SPACES
	num_spaces = 0;
#endif
#ifdef HAVE_TEMPS
	num_temps = 0;
#endif
#ifdef HAVE_GPIOS
	num_gpios = 0;
#endif
	Serial.write(CMD_ID);
	for (uint8_t i = 0; i < 8; ++i)
		Serial.write(uint8_t(0));
	arch_setup_end();
}
