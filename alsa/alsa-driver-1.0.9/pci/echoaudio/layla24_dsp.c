// ****************************************************************************
//
//   Copyright Echo Digital Audio Corporation (c) 1998 - 2004
//   All rights reserved
//   www.echoaudio.com
//
//   This file is part of Echo Digital Audio's generic driver library.
//
//   Echo Digital Audio's generic driver library is free software;
//   you can redistribute it and/or modify it under the terms of
//   the GNU General Public License as published by the Free Software Foundation.
//
//   This program is distributed in the hope that it will be useful,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//   GNU General Public License for more details.
//
//   You should have received a copy of the GNU General Public License
//   along with this program; if not, write to the Free Software
//   Foundation, Inc., 59 Temple Place - Suite 330, Boston,
//   MA  02111-1307, USA.
//
// ****************************************************************************
//
// Translation from C++ and adaptation for use in ALSA-Driver
// were made by Giuliano Pochini <pochini@shiny.it>
//
// ****************************************************************************


static int write_control_reg(echoaudio_t *chip, u32 value, char force);
static int set_input_clock(echoaudio_t *chip, u16 clock);
static int set_professional_spdif(echoaudio_t *chip, char prof);
static int set_nominal_level(echoaudio_t *chip, u16 index, char consumer);
static int set_digital_mode(echoaudio_t *chip, u8 mode);
static int load_asic_generic(echoaudio_t *chip, u32 cmd, const struct firmware *asic);
static int check_asic_status(echoaudio_t *chip);


static int init_hw(echoaudio_t *chip, u16 device_id, u16 subdevice_id)
{
	int err, i;

	DE_INIT(("init_hw() - Layla24\n"));
	snd_assert((subdevice_id & 0xfff0) == LAYLA24, return -ENODEV);

	/* This part is common to all the cards */
	if ((err = init_dsp_comm_page(chip))) {
		DE_INIT(("init_hw - could not initialize DSP comm page\n"));
		return err;
	}

	chip->device_id = device_id;
	chip->subdevice_id = subdevice_id;
	chip->bad_board = TRUE;
	chip->has_midi = TRUE;
	chip->dsp_code_to_load = &card_fw[FW_LAYLA24_DSP];
	chip->input_clock_types =	ECHO_CLOCK_BIT_INTERNAL | ECHO_CLOCK_BIT_SPDIF |
					ECHO_CLOCK_BIT_WORD | ECHO_CLOCK_BIT_ADAT;
	chip->digital_modes =		ECHOCAPS_HAS_DIGITAL_MODE_SPDIF_RCA |
					ECHOCAPS_HAS_DIGITAL_MODE_SPDIF_OPTICAL |
					ECHOCAPS_HAS_DIGITAL_MODE_ADAT;
	chip->digital_mode =		DIGITAL_MODE_SPDIF_RCA;
	chip->professional_spdif = FALSE;
	chip->digital_in_automute = TRUE;

	/* Load the DSP and the ASIC on the PCI card */
	if ((err = load_firmware(chip)) < 0)
		return err;

	chip->bad_board = FALSE;

	/* Must call this here after DSP is init to init gains and mutes */
	if ((err = init_line_levels(chip)) < 0)
		return err;

	/* Set professional nominal levels (FALSE is +4dBu) */
	for (i = 0; i < num_analog_busses_out(chip); i++)
		err = set_nominal_level(chip, i, FALSE);

	for (i = 0; i < num_analog_busses_in(chip); i++)
		err = set_nominal_level(chip, bx_analog_in(chip) + i, FALSE);

	/* Set the digital mode to S/PDIF RCA */
	set_digital_mode(chip, DIGITAL_MODE_SPDIF_RCA);

	/* Set the S/PDIF output format to "professional" */
	err = set_professional_spdif(chip, TRUE);

	DE_INIT(("init_hw done\n"));
	return err;
}



//===========================================================================
//
// detect_input_clocks returns a bitmask consisting of all the input
// clocks currently connected to the hardware; this changes as the user
// connects and disconnects clock inputs.
//
// You should use this information to determine which clocks the user is
// allowed to select.
//
// Layla24 supports S/PDIF, word, and ADAT input clocks.
//
//===========================================================================

static u32 detect_input_clocks(const echoaudio_t *chip)
{
	u32 clocks_from_dsp, clock_bits;

	/* Map the DSP clock detect bits to the generic driver clock detect bits */
	clocks_from_dsp = le32_to_cpu(chip->comm_page->status_clocks);

	clock_bits = ECHO_CLOCK_BIT_INTERNAL;

	if (clocks_from_dsp & GML_CLOCK_DETECT_BIT_SPDIF)
		clock_bits |= ECHO_CLOCK_BIT_SPDIF;

	if (clocks_from_dsp & GML_CLOCK_DETECT_BIT_ADAT)
		clock_bits |= ECHO_CLOCK_BIT_ADAT;

	if (clocks_from_dsp & GML_CLOCK_DETECT_BIT_WORD)
		clock_bits |= ECHO_CLOCK_BIT_WORD;

	return clock_bits;
}



/****************************************************************************

  Hardware setup and config

 ****************************************************************************/

//===========================================================================
//
// Layla24 has an ASIC on the PCI card and another ASIC in the external box;
// both need to be loaded.
//
//===========================================================================

static int load_asic(echoaudio_t *chip)
{
	int err;

	if (chip->asic_loaded)
		return 1;

	DE_INIT(("load_asic\n"));

	/* Give the DSP a few milliseconds to settle down */
	mdelay(10);

	/* Load the ASIC for the PCI card */
	err = load_asic_generic(chip, DSP_FNC_LOAD_LAYLA24_PCI_CARD_ASIC, &card_fw[FW_LAYLA24_1_ASIC]);
	if (err < 0)
		return err;

	chip->asic_code = &card_fw[FW_LAYLA24_2S_ASIC];

	/* Now give the new ASIC a little time to set up */
	mdelay(10);

	/* Do the external one */
	err = load_asic_generic(chip, DSP_FNC_LOAD_LAYLA24_EXTERNAL_ASIC, &card_fw[FW_LAYLA24_2S_ASIC]);
	if (err < 0)
		return FALSE;

	/* Now give the external ASIC a little time to set up */
	mdelay(10);

	/* See if it worked */
	err = check_asic_status(chip);

	/* Set up the control register if the load succeeded - 48 kHz, internal clock, S/PDIF RCA mode */
	if (!err)
		err = write_control_reg(chip, GML_CONVERTER_ENABLE | GML_48KHZ, TRUE);

	DE_INIT(("load_asic() done\n"));
	return err;
}



//===========================================================================
//
// set_sample_rate
//
// Set the sample rate for Layla24
//
// Layla24 is simple; just send it the sampling rate (assuming that the clock
// mode is correct).
//
//===========================================================================

static int set_sample_rate(echoaudio_t *chip, u32 rate)
{
	u32 control_reg, clock, base_rate;

	snd_assert(rate < 50000 || chip->digital_mode != DIGITAL_MODE_ADAT, return -EINVAL);

	/* Only set the clock for internal mode. */
	if (chip->input_clock != ECHO_CLOCK_INTERNAL) {
		DE_ACT(("set_sample_rate: Cannot set sample rate - clock not set to CLK_CLOCKININTERNAL\n"));
		/* Save the rate anyhow */
		chip->comm_page->sample_rate = cpu_to_le32(rate);
		chip->sample_rate = rate;
		return 0;
	}

	/* Get the control register & clear the appropriate bits */
	control_reg = le32_to_cpu(chip->comm_page->control_register);
	control_reg &= GML_CLOCK_CLEAR_MASK & GML_SPDIF_RATE_CLEAR_MASK;

	clock = 0;

	switch (rate) {
	case 96000:
		clock = GML_96KHZ;
		break;
	case 88200:
		clock = GML_88KHZ;
		break;
	case 48000:
		clock = GML_48KHZ | GML_SPDIF_SAMPLE_RATE1;
		break;
	case 44100:
		clock = GML_44KHZ;
		/* Professional mode */
		if (control_reg & GML_SPDIF_PRO_MODE)
			clock |= GML_SPDIF_SAMPLE_RATE0;
		break;
	case 32000:
		clock = GML_32KHZ | GML_SPDIF_SAMPLE_RATE0 | GML_SPDIF_SAMPLE_RATE1;
		break;
	case 22050:
		clock = GML_22KHZ;
		break;
	case 16000:
		clock = GML_16KHZ;
		break;
	case 11025:
		clock = GML_11KHZ;
		break;
	case 8000:
		clock = GML_8KHZ;
		break;
	default:
		/* If this is a non-standard rate, then the driver needs to
		use Layla24's special "continuous frequency" mode */
		clock = LAYLA24_CONTINUOUS_CLOCK;
		if (rate > 50000) {
			base_rate = rate >> 1;
			control_reg |= GML_DOUBLE_SPEED_MODE;
		} else {
			base_rate = rate;
		}

		if (base_rate < 25000)
			base_rate = 25000;

		if (wait_handshake(chip))
			return -EIO;

		chip->comm_page->sample_rate = cpu_to_le32(LAYLA24_MAGIC_NUMBER / base_rate - 2);

		clear_handshake(chip);
		send_vector(chip, DSP_VC_SET_LAYLA24_FREQUENCY_REG);
	}

	control_reg |= clock;

	chip->comm_page->sample_rate = cpu_to_le32(rate);	/* ignored by the DSP ? */
	chip->sample_rate = rate;
	DE_ACT(("set_sample_rate: %d clock %d\n", rate, control_reg));

	return write_control_reg(chip, control_reg, FALSE);
}



//===========================================================================
//
// set_input_clock
//
//===========================================================================

static int set_input_clock(echoaudio_t *chip, u16 clock)
{
	u32 control_reg, clocks_from_dsp;

	/* Mask off the clock select bits */
	control_reg = le32_to_cpu(chip->comm_page->control_register) & GML_CLOCK_CLEAR_MASK;
	clocks_from_dsp = le32_to_cpu(chip->comm_page->status_clocks);

	/* Pick the new clock */
	switch (clock) {
	case ECHO_CLOCK_INTERNAL:
		DE_ACT(("Set Layla24 clock to INTERNAL\n"));
		chip->input_clock = ECHO_CLOCK_INTERNAL;
		return set_sample_rate(chip, chip->sample_rate);
	case ECHO_CLOCK_SPDIF:
		if (chip->digital_mode == DIGITAL_MODE_ADAT)
			return -EAGAIN;
		control_reg |= GML_SPDIF_CLOCK;
		/* Layla24 doesn't support 96KHz S/PDIF */
		control_reg &= ~GML_DOUBLE_SPEED_MODE;
		DE_ACT(("Set Layla24 clock to SPDIF\n"));
		break;
	case ECHO_CLOCK_WORD:
		control_reg |= GML_WORD_CLOCK;
		if (clocks_from_dsp & GML_CLOCK_DETECT_BIT_WORD96)
			control_reg |= GML_DOUBLE_SPEED_MODE;
		else
			control_reg &= ~GML_DOUBLE_SPEED_MODE;
		DE_ACT(("Set Layla24 clock to WORD\n"));
		break;
	case ECHO_CLOCK_ADAT:
		if (chip->digital_mode != DIGITAL_MODE_ADAT)
			return -EAGAIN;
		control_reg |= GML_ADAT_CLOCK;
		control_reg &= ~GML_DOUBLE_SPEED_MODE;
		DE_ACT(("Set Layla24 clock to ADAT\n"));
		break;
	default:
		DE_ACT(("Input clock 0x%x not supported for Layla24\n", clock));
		return -EINVAL;
	}

	chip->input_clock = clock;
	return write_control_reg(chip, control_reg, TRUE);
}



//===========================================================================
//
// Depending on what digital mode you want, Layla24 needs different ASICs
// loaded.  This function checks the ASIC needed for the new mode and sees
// if it matches the one already loaded.
//
//===========================================================================

static int switch_asic(echoaudio_t *chip, const struct firmware *asic)
{
	s8 *monitors;

	/*  Check to see if this is already loaded */
	if (asic != chip->asic_code) {
		monitors = kmalloc(MONITOR_ARRAY_SIZE, GFP_KERNEL);
		if (! monitors)
			return -ENOMEM;

		memcpy(monitors, chip->comm_page->monitors, MONITOR_ARRAY_SIZE);
		memset(chip->comm_page->monitors, ECHOGAIN_MUTED, MONITOR_ARRAY_SIZE);

		/* Load the desired ASIC */
		if (load_asic_generic(chip, DSP_FNC_LOAD_LAYLA24_EXTERNAL_ASIC, asic) < 0) {
			memcpy(chip->comm_page->monitors, monitors, MONITOR_ARRAY_SIZE);
			kfree(monitors);
			return -EIO;
		}
		chip->asic_code = asic;
		memcpy(chip->comm_page->monitors, monitors, MONITOR_ARRAY_SIZE);
		kfree(monitors);
	}

	return 0;
}



//===========================================================================
//
// set_digital_mode
//
//===========================================================================

static int dsp_set_digital_mode(echoaudio_t *chip, u8 mode)
{
	u32 control_reg;
	int err, incompatible_clock;
	const struct firmware *asic;

	/* Set clock to "internal" if it's not compatible with the new mode */
	incompatible_clock = FALSE;
	switch (mode) {
	case DIGITAL_MODE_SPDIF_OPTICAL:
	case DIGITAL_MODE_SPDIF_RCA:
		if (chip->input_clock == ECHO_CLOCK_ADAT)
			incompatible_clock = TRUE;
		asic = &card_fw[FW_LAYLA24_2S_ASIC];
		break;
	case DIGITAL_MODE_ADAT:
		if (chip->input_clock == ECHO_CLOCK_SPDIF)
			incompatible_clock = TRUE;
		asic = &card_fw[FW_LAYLA24_2A_ASIC];
		break;
	default:
		DE_ACT(("Digital mode not supported: %d\n", mode));
		return -EINVAL;
	}

	if (incompatible_clock) {	/* Switch to 48KHz, internal */
		chip->sample_rate = 48000;
		spin_lock_irq(&chip->lock);
		set_input_clock(chip, ECHO_CLOCK_INTERNAL);
		spin_unlock_irq(&chip->lock);
	}

	/* switch_asic() can sleep */
	if (switch_asic(chip, asic) < 0)
		return -EIO;

	spin_lock_irq(&chip->lock);

	/* Tweak the control register */
	control_reg = le32_to_cpu(chip->comm_page->control_register);
	control_reg &= GML_DIGITAL_MODE_CLEAR_MASK;

	switch (mode) {
	case DIGITAL_MODE_SPDIF_OPTICAL:
		control_reg |= GML_SPDIF_OPTICAL_MODE;
		break;
	case DIGITAL_MODE_SPDIF_RCA:
		/* GML_SPDIF_OPTICAL_MODE bit cleared */
		break;
	case DIGITAL_MODE_ADAT:
		control_reg |= GML_ADAT_MODE;
		control_reg &= ~GML_DOUBLE_SPEED_MODE;
		break;
	}

	err = write_control_reg(chip, control_reg, TRUE);
	spin_unlock_irq(&chip->lock);
	if (err < 0)
		return err;
	chip->digital_mode = mode;

	DE_ACT(("set_digital_mode to %d\n", mode));
	return incompatible_clock;
}
