#include "serial.h"

static void _set_bit (int);
void    rrdtool_update (char *, float);
void    rrdtool_update_consumption (float, float);

void    kw1281_handle_error (void);
void    kw1281_send_byte_ack (unsigned char);
void    kw1281_send_ack (void);
void    kw1281_send_block (unsigned char);
void    kw1281_recv_block (unsigned char);
void    kw1281_print (void);
int     kw1281_inc_counter ();
unsigned char kw1281_recv_byte_ack (void);

float   const_multiplier = 0.00000089;
float   const_inj_subtract = 0.1;

int     fd;
int     counter;		// kw1281 protocol block counter
int     ready = 0;



void
rrdtool_update_consumption (float km, float h)
{
    pid_t   pid;
    int     status;
    time_t  t;

    if ((pid = fork ()) == 0)
    {
	char    cmd[256];

	snprintf (cmd, sizeof (cmd), "%d:%.2f:%.2f", (int) time (&t), km, h);
	execlp ("rrdtool", "rrdtool", "update", "consumption.rrd", cmd, NULL);
	exit (-1);
    }

    waitpid (pid, &status, 0);

    if ((pid = fork ()) == 0)
    {
	char    starttime[256];
	char    endtime[256];

	snprintf (starttime, sizeof (starttime), "%d", (int) time (&t) - 300);
	snprintf (endtime, sizeof (endtime), "%d", (int) time (&t));

	execlp ("rrdtool", "rrdtool", "graph", "consumption.png",
		"--start", starttime, "--end", endtime,
		"DEF:con_km=consumption.rrd:km:AVERAGE",
		"AREA:con_km#990000:l/100km",
		"DEF:con_h=consumption.rrd:h:AVERAGE",
		"LINE3:con_h#009999:l/h", NULL);

	exit (-1);
    }

    waitpid (pid, &status, 0);

    return;
}

/* execute the rddtool command
 * name = rdd file to write val to
 * val = value
 */
void
rrdtool_update (char *name, float val)
{
    pid_t   pid;
    int     status;
    time_t  t;

    if ((pid = fork ()) == 0)
    {
	char    cmd[256];
	char    rrd[256];

	// rrdtool update
	snprintf (rrd, sizeof (rrd), "%s.rrd", name);
	snprintf (cmd, sizeof (cmd), "%d:%.2f", (int) time (&t), val);
	execlp ("rrdtool", "rrdtool", "update", rrd, cmd, NULL);
	exit (-1);
    }

    waitpid (pid, &status, 0);

    if ((pid = fork ()) == 0)
    {
	char    png[256];
	char    starttime[256];
	char    endtime[256];
	char    def[256];
	char    line[256];

	snprintf (png, sizeof (png), "%s.png", name);
	snprintf (starttime, sizeof (starttime), "%d", (int) time (&t) - 300);
	snprintf (endtime, sizeof (endtime), "%d", (int) time (&t));
	snprintf (def, sizeof (def), "DEF:my%s=%s.rrd:%s:AVERAGE", name, name,
		  name);
	snprintf (line, sizeof (line), "LINE2:my%s#0000FF:%s", name, name);

	execlp ("rrdtool", "rrdtool", "graph", png,
		"--start", starttime, "--end", endtime, def, line, NULL);

	exit (-1);
    }

    waitpid (pid, &status, 0);

    return;
}

void
rrdtool_create_consumption (void)
{
    pid_t   pid;
    int     status;
    time_t  t;
    FILE   *fp;

    fp = fopen ("consumption.rrd", "rw");
    if (fp)
    {
	fclose (fp);
	return;
    }

    printf ("creating consumption rrd file\n");

    /*
       // remove old file
       if (unlink(cmd) == -1)
       perror("could not delete old .rrd file");
     */

    if ((pid = fork ()) == 0)
    {
	char    starttime[256];

	snprintf (starttime, sizeof (starttime), "%d", (int) time (&t));

	// after 15secs: unknown value
	// 1. RRA last 5 mins
	// 2. RRA last 30mins
	// 3. RRA one value (average consumption last 30mins)
	execlp ("rrdtool", "rrdtool", "create", "consumption.rrd",
		"--start", starttime, "--step", "1",
		"DS:km:GAUGE:15:U:U", "DS:h:GAUGE:15:U:U",
		"RRA:AVERAGE:0.5:1:300", "RRA:AVERAGE:0.5:5:360",
		"RRA:AVERAGE:0.5:1800:1", NULL);
	exit (-1);
    }

    waitpid (pid, &status, 0);

    return;
}

void
rrdtool_create (char *name)
{
    pid_t   pid;
    int     status;
    time_t  t;
    char    cmd[256];
    FILE   *fp;

    snprintf (cmd, sizeof (cmd), "%s.rrd", name);

    fp = fopen (cmd, "rw");
    if (fp)
    {
	fclose (fp);
	return;
    }

    printf ("creating %s\n", cmd);

    /*
       // remove old file
       if (unlink(cmd) == -1)
       perror("could not delete old .rrd file");
     */

    if ((pid = fork ()) == 0)
    {
	char    rrd[256];
	char    starttime[256];
	char    ds[256];

	snprintf (rrd, sizeof (rrd), "%s.rrd", name);
	snprintf (starttime, sizeof (starttime), "%d", (int) time (&t));
	snprintf (ds, sizeof (ds), "DS:%s:GAUGE:15:U:U", name);


	// rrdtool create
	execlp ("rrdtool", "rrdtool", "create", rrd,
		"--start", starttime, "--step", "1",
		ds, "RRA:AVERAGE:0.5:1:300", "RRA:AVERAGE:0.5:5:360",
		"RRA:AVERAGE:0.5:50:288", "RRA:AVERAGE:0.5:1800:1", NULL);
	exit (-1);
    }

    waitpid (pid, &status, 0);

    return;
}


/* manually set serial lines */
static void
_set_bit (int bit)
{
    int     flags;

    ioctl (fd, TIOCMGET, &flags);

    if (bit)
    {
	ioctl (fd, TIOCCBRK, 0);
	flags &= ~TIOCM_RTS;
    }
    else
    {
	ioctl (fd, TIOCSBRK, 0);
	flags |= TIOCM_RTS;
    }

    ioctl (fd, TIOCMSET, &flags);
}

/* function in case an error occures */
void
kw1281_handle_error (void)
{
    /*
     * recv() until 0x8a
     * then send 0x75 (complement)
     * reset counter = 1
     * continue with block readings
     *
     *  or just exit -1 and start program in a loop
     */

    close (fd);
    pthread_exit (NULL);
}

// increment the counter
int
kw1281_inc_counter (void)
{
    if (counter == 255)
    {
	counter = 1;
	return 255;
    }
    else
	counter++;

    return counter - 1;
}

/* receive one byte and acknowledge it */
unsigned char
kw1281_recv_byte_ack (void)
{
    unsigned char c, d;

    read (fd, &c, 1);
    d = 0xff - c;
    usleep (WRITE_DELAY);
    write (fd, &d, 1);
    read (fd, &d, 1);
    if (0xff - c != d)
    {
	printf ("kw1281_recv_byte_ack: echo error recv: 0x%02x (!= 0x%02x)\n",
		d, 0xff - c);

	kw1281_handle_error ();
    }
    return c;
}

/* send one byte and wait for acknowledgement */
void
kw1281_send_byte_ack (unsigned char c)
{
    unsigned char d;

    usleep (WRITE_DELAY);
    write (fd, &c, 1);
    read (fd, &d, 1);
    if (c != d)
    {
	printf ("kw1281_send_byte_ack: echo error (0x%02x != 0x%02x)\n", c,
		d);
	kw1281_handle_error ();
    }

    read (fd, &d, 1);
    if (0xff - c != d)
    {
	printf ("kw1281_send_byte_ack: ack error (0x%02x != 0x%02x)\n",
		0xff - c, d);
	kw1281_handle_error ();
    }
}

/* write 7O1 address byte at 5 baud and wait for sync/keyword bytes */
void
kw1281_init (int address)
{
    int     i, p, flags;
    unsigned char c;

    int     in;

    // prepare to send (clear dtr and rts)
    ioctl (fd, TIOCMGET, &flags);
    flags &= ~(TIOCM_DTR | TIOCM_RTS);
    ioctl (fd, TIOCMSET, &flags);
    usleep (INIT_DELAY);

    _set_bit (0);		// start bit
    usleep (INIT_DELAY);	// 5 baud
    p = 1;
    for (i = 0; i < 7; i++)
    {
	// address bits, lsb first
	int     bit = (address >> i) & 0x1;
	_set_bit (bit);
	p = p ^ bit;
	usleep (INIT_DELAY);
    }
    _set_bit (p);		// odd parity
    usleep (INIT_DELAY);
    _set_bit (1);		// stop bit
    usleep (INIT_DELAY);

    // set dtr
    ioctl (fd, TIOCMGET, &flags);
    flags |= TIOCM_DTR;
    ioctl (fd, TIOCMSET, &flags);

    // read bogus values, if any
    ioctl (fd, FIONREAD, &in);
    while (in--)
    {
	read (fd, &c, 1);
#ifdef DEBUG
	printf ("ignore 0x%02x\n", c);
#endif
    }

    read (fd, &c, 1);
#ifdef DEBUG
    printf ("read 0x%02x\n", c);
#endif

    read (fd, &c, 1);
#ifdef DEBUG
    printf ("read 0x%02x\n", c);
#endif

    c = kw1281_recv_byte_ack ();
#ifdef DEBUG
    printf ("read 0x%02x (and sent ack)\n", c);
#endif

    counter = 1;

    return;
}

/* send an ACK block */
void
kw1281_send_ack ()
{
    unsigned char c;

#ifdef DEBUG
    printf ("send ACK block %d\n", counter);
#endif

    /* block length */
    kw1281_send_byte_ack (0x03);

    kw1281_send_byte_ack (kw1281_inc_counter ());

    /* ack command */
    kw1281_send_byte_ack (0x09);

    /* block end */
    c = 0x03;
    usleep (WRITE_DELAY);
    write (fd, &c, 1);
    read (fd, &c, 1);
    if (c != 0x03)
    {
	printf ("echo error (0x03 != 0x%02x)\n", c);
	kw1281_handle_error ();
    }

    return;
}

/* send group reading block */
void
kw1281_send_block (unsigned char n)
{
    unsigned char c;

#ifdef DEBUG
    printf ("send group reading block %d\n", counter);
#endif

    /* block length */
    kw1281_send_byte_ack (0x04);

    // counter
    kw1281_send_byte_ack (kw1281_inc_counter ());

    /*  type group reading */
    kw1281_send_byte_ack (0x29);

    /* which group block */
    kw1281_send_byte_ack (n);

    /* block end */
    c = 0x03;
    usleep (WRITE_DELAY);
    write (fd, &c, 1);
    read (fd, &c, 1);
    if (c != 0x03)
    {
	printf ("echo error (0x03 != 0x%02x)\n", c);
	kw1281_handle_error ();
    }
    return;
}

/* receive a complete block */
void
kw1281_recv_block (unsigned char n)
{
    int     i;
    unsigned char c, l, t;
    unsigned char buf[256];

    /* block length */
    l = kw1281_recv_byte_ack ();

    c = kw1281_recv_byte_ack ();

    if (c != counter)
    {
	printf ("counter error (%d != %d)\n", counter, c);

#ifdef DEBUG
	printf ("IN   OUT\t(block dump)\n");
	printf ("0x%02x\t\t(block length)\n", l);
	printf ("     0x%02x\t(ack)\n", 0xff - l);
	printf ("0x%02x\t\t(counter)\n", c);
	printf ("     0x%02x\t(ack)\n", 0xff - c);
	/*
	   while (1) {
	   c = kw1281_recv_byte_ack();
	   printf("0x%02x\t\t(data)\n", c);
	   printf("     0x%02x\t(ack)\n", 0xff - c);
	   }
	 */
#endif

	kw1281_handle_error ();
    }

    t = kw1281_recv_byte_ack ();

#ifdef DEBUG
    switch (t)
    {
	case 0xf6:
	    printf ("got ASCII block %d\n", counter);
	    break;
	case 0x09:
	    printf ("got ACK block %d\n", counter);
	    break;
	case 0x00:
	    printf ("got 0x00 block %d\n", counter);
	case 0xe7:
	    printf ("got group reading answer block %d\n", counter);
	    break;
	default:
	    printf ("block title: 0x%02x (block %d)\n", t, counter);
	    break;
    }
#endif

    l -= 2;

    i = 0;
    while (--l)
    {
	c = kw1281_recv_byte_ack ();

	buf[i++] = c;

#ifdef DEBUG
	printf ("0x%02x ", c);
#endif

    }
    buf[i] = 0;

#ifdef DEBUG
    if (t == 0xf6)
	printf ("= \"%s\"\n", buf);
#endif

    if (t == 0xe7)
    {

	// look at field headers 0, 3, 6, 9
	for (i = 0; i <= 9; i += 3)
	{
	    switch (buf[i])
	    {
		case 0x01:	// rpm
		    if (i == 0)
			rpm = 0.2 * buf[i + 1] * buf[i + 2];
		    break;

		case 0x21:	// load
		    if (i == 0)
		    {
			if (buf[i + 1] == 0)
			    load = 100;
			else
			    load = 100 * buf[i + 2] / buf[i + 1];
		    }
		    break;

		case 0x0f:	// injection time
		    inj_time = 0.01 * buf[i + 1] * buf[i + 2];
		    break;

		case 0x12:	// absolute pressure
		    oil_press = 0.04 * buf[i + 1] * buf[i + 2];
		    break;

		case 0x05:	// temperature
		    if (i == 6)
			temp1 = buf[i + 1] * (buf[i + 2] - 100) * 0.1;
		    if (i == 9)
			temp2 = buf[i + 1] * (buf[i + 2] - 100) * 0.1;
		    break;

		case 0x07:	// speed
		    speed = 0.01 * buf[i + 1] * buf[i + 2];
		    break;

		case 0x15:	// battery voltage
		    voltage = 0.001 * buf[i + 1] * buf[i + 2];
		    break;

		default:
#ifdef DEBUG
		    printf ("unknown value: 0x%02x: a = %d, b = %d\n",
			    buf[i], buf[i + 1], buf[i + 2]);
#endif
		    break;
	    }

	}

    }
#ifdef DEBUG
    else
	printf ("\n");
#endif

    /* read block end */
    read (fd, &c, 1);
    if (c != 0x03)
    {
	printf ("block end error (0x03 != 0x%02x)\n", c);
	kw1281_handle_error ();
    }

    kw1281_inc_counter ();

    // set ready flag when receiving ack block
    if (t == 0x09 && !ready)
    {
	ready = 1;
    }
    // set ready flag when sending 0x00 block after errors
    if (t == 0x00 && !ready)
    {
	ready = 1;
    }

}


int
kw1281_open (char *device)
{
    struct termios newtio;
    struct serial_struct st, ot;

    // open the serial device
    if ((fd = open (device, O_SYNC | O_RDWR | O_NOCTTY)) < 0)
    {
	printf ("couldn't open serial device %s.\n", device);
	return -1;
    }

    if (ioctl (fd, TIOCGSERIAL, &ot) < 0)
    {
	printf ("getting tio failed\n");
	return -1;
    }
    memcpy (&st, &ot, sizeof (ot));

    // setting custom baud rate
    st.custom_divisor = st.baud_base / BAUDRATE;
    st.flags &= ~ASYNC_SPD_MASK;
    st.flags |= ASYNC_SPD_CUST | ASYNC_LOW_LATENCY;
    if (ioctl (fd, TIOCSSERIAL, &st) < 0)
    {
	printf ("TIOCSSERIAL failed\n");
	return -1;
    }
    newtio.c_cflag = B38400 | CLOCAL | CREAD;	// 38400 baud, so custom baud rate above works
    newtio.c_iflag = IGNPAR;	// ICRNL provokes bogus replys after block 12
    newtio.c_oflag = 0;
    newtio.c_cc[VMIN] = 1;
    newtio.c_cc[VTIME] = 0;
    tcflush (fd, TCIFLUSH);
    tcsetattr (fd, TCSANOW, &newtio);

    return 0;
}

/* this function prints the collected values */
void
kw1281_print (void)
{
    printf ("----------------------------------------\n");
    printf ("l/h\t\t%.2f\n", con_h);
    printf ("l/100km\t\t%.2f\n", con_km);
    printf ("speed\t\t%.1f km/h\n", speed);
    printf ("rpm\t\t%.0f RPM\n", rpm);
    printf ("inj on time\t%.2f ms\n", inj_time);
    printf ("temp1\t\t%.1f °C\n", temp1);
    printf ("temp2\t\t%.1f °C\n", temp2);
    printf ("voltage\t\t%.2f V\n", voltage);
    printf ("load\t\t%.0f %%\n", load);
    printf ("absolute press\t%.0f mbar\n", oil_press);
    printf ("counter\t\t%d\n", counter);
    printf ("\n");

    return;
}

void   *
kw1281_mainloop ()
{

#ifdef SERIAL_ATTACHED
    if (kw1281_open ("/dev/ttyUSB0") == -1)
	pthread_exit (NULL);

    printf ("init\n");		// ECU: 0x01, INSTR: 0x17
    kw1281_init (0x01);		// send 5baud address, read sync byte + key word
#endif


#ifdef DEBUG
    printf ("receive blocks\n");
#endif


#ifndef SERIAL_ATTACHED
    /* 
     * this block is for testing purposes
     * when the car is too far to test
     * the html interface / ajax server
     */

    printf ("incrementing values for testing purposes...\n");
    speed = 10;
    con_km = 0.1;
    load = 0;
    con_h = 0.01;
    temp1 = 20;
    temp2 = 0;
    voltage = 3.00;

    for (;;)
    {
	speed++;;
	con_km += 0.1;
	con_h += 0.03;
	temp1++;
	temp2++;
	voltage += 0.15;
	load += 3;
	usleep (2000000);
	rrdtool_update ("speed", speed);
	rrdtool_update_consumption (con_km, con_h);
    }
#endif


    while (!ready)
    {
	kw1281_recv_block (0x00);
	if (!ready)
	    kw1281_send_ack ();
    }

    printf ("init done.\n");
    for (;;)
    {
	// request block 0x02
	kw1281_send_block (0x02);
	kw1281_recv_block (0x02);	// inj_time, rpm, load, oil_press

	// calculate consumption per hour
	if (inj_time > const_inj_subtract)
	    con_h = 60 * 4 * const_multiplier *
		rpm * (inj_time - const_inj_subtract);
	else
	    con_h = 0;

	// rrdtool_update ("rpm", rpm);

	// request block 0x05
	kw1281_send_block (0x05);
	kw1281_recv_block (0x05);	// in this block is speed

	// calculate consumption per hour
	if (speed > 0)
	    con_km = (con_h / speed) * 100;
	else
	    con_km = -1;

	// update rrdtool databases
	rrdtool_update ("speed", speed);
	rrdtool_update_consumption (con_km, con_h);

	// request block 0x04
	kw1281_send_block (0x04);
	kw1281_recv_block (0x04);	// temperatures + voltage

	// update rrdtool databases
	/*
	   rrdtool_update ("temp1", temp1);
	   rrdtool_update ("temp2", temp2);
	   rrdtool_update ("voltage", voltage);
	 */

	// output values
	kw1281_print ();
    }
}
