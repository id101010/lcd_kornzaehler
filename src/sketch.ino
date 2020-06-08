#include <LCD03.h>
#include <Wire.h>

#define F_TIMER 1000000 // timer base frequency 10ms
#define AVG_INT 2000 // main interval 2s (2000 * timeroverflow_time)
#define FIFO_SIZE 20 // maximum number of values in the fifo
 
LCD03 lcd; // LCD object

// operational vars
int i = 0;
volatile long cnt = 0; // counter
volatile int interval = 0; // freq of std deviation calc
volatile int timer_0_overflows = 0; // milliseconds
volatile float t_diff = 0; // time difference
volatile bool flag_timer = false; // timer flag
volatile bool flag_send = false; // send flag

// vars for statistics
volatile float t_fifo[FIFO_SIZE];
volatile int nfifo              = 0;
volatile float average, variance, std_deviation, sum = 0, sum1 = 0;

/*
 * Calculate the std deviation within the fifo.
 */
void std_dev(void)
{
    sum = 0;
    sum1 = 0;

    // sum of all elements
    for(i = 0; i < FIFO_SIZE; i++){
        sum = sum + t_fifo[i];
    }

    // average
    average = sum / FIFO_SIZE;

    // compute variance and std deviation
    for(i = 0; i < FIFO_SIZE; i++){
        sum1 = sum1 + pow((t_fifo[i] - average), 2);
    }

    variance = sum1 / FIFO_SIZE;
    std_deviation = sqrt(variance);
}

/*
 * Push a value into the fifo.
 */
void push_fifo(float t)
{
    t_fifo[nfifo] = t; // push value t to fifo
    nfifo = (nfifo + 1) % FIFO_SIZE; // increment fifo index within boundary
}

/*
 * Init the interrupt registers.
 */
void init_interrupt(void)
{
    EICRA = (1 << ISC01) | (1 << ISC00); // INT0 on rising edge
    EIMSK = (1 << INT0); // enable INT0
    sei(); // enable the globald interrupt pin
}

/*
 * Init and start timer0 in CTC mode so it will overflow every 1ms.
 */
void start_timer()
{
    TCCR0A  = (1<<WGM01); // set CTC Mode
    TCCR0B &= ~(1<<CS02); // set prescaler to F_CPU/64
    TCCR0B |= (1<<CS01); //  `-> second bit of register
    TCCR0B |= (1<<CS00); //  `-> third bit of register
    OCR0A   = 250-1;      // set OCR0A = F_CPU/64/1ms^-1
    TIMSK0 |= (1<<OCIE0A); // enable Timer0 Compare Match Interrupt
    timer_0_overflows = 0; // reset timer overflow counter
}

/*
 * Stop the timer0 and return the timeroverflows.
 */
unsigned int stop_timer()
{
    TCCR0B = 0; // Stop Timer 0
    return timer_0_overflows;
}

/*
 * Printfunction to display values on the lcd screen.
 */
void lcd_print_values(float avg_time, float stddev_time, int number)
{
    // Print data on lcd
    lcd.setCursor(0,0);
    lcd.print("<t>:            ");
    lcd.setCursor(4,0);
    lcd.print(String(avg_time));

    lcd.setCursor(0,1);
    lcd.print("S:              ");
    lcd.setCursor(2,1);
    lcd.print(String(stddev_time));
    
    lcd.setCursor(8,1);
    lcd.print("n:");
    lcd.setCursor(10,1);
    lcd.print(String(number));
}

/*
 * Send information to the serial console.
 * int count: current number of grains
 * int diff: timedifference between the last two grains
 */
void uart_print_values(long count, float diff)
{
    // print data on serial interface (as csv values)
    Serial.print(String(count));
    Serial.print(',');
    Serial.print(String(diff));
    Serial.print("\r\n");
}

/*
 * Interrupt service routine
 * for the controllers INT0 pin
 */
ISR(INT0_vect)
{
    int tmp = 0;
    cnt++; // increment counter

    if(!flag_send){
        flag_send = true; // set ouput flag
    }

    if(!flag_timer){
        start_timer(); // start the timer 
        flag_timer  = true; // set the timer flag
    }
    else{
        tmp = stop_timer(); // stop timer
        
        if(tmp <= 1000 && tmp >= 0){ // if the value is greater than 1s but not negative
            t_diff = tmp; // write the timer value to t_diff
        }
        
        start_timer(); // restart the timer
    }
}

/*
 * Interrupt service routine
 * for timer0, gets called each n miliseconds (see timer config)
 */
ISR(TIMER0_COMPA_vect)
{
    timer_0_overflows++; // increment overflows
    interval++; // increment interval counter
}

/*
 * Initialize the system
 */
void setup()
{
    lcd.begin(16, 2); // set LCD as 16x2 screen
    lcd.backlight(); // set backlight

    Serial.begin(9600); // init serial interface

    // initialize fifo with zeros
    for(i = 0; i < FIFO_SIZE; i++){
        t_fifo[i] = 0;
    }

    lcd_print_values(0,0,0);
    flag_timer  = false;
    flag_send   = false;
    pinMode(5, OUTPUT);

    init_interrupt();
}
 
/*
 * main
 */
void loop()
{
    // each 2 seconds
    if(interval == AVG_INT){
        interval = 0;
        std_dev(); // calculate stddev
        //push_fifo(0); // push a zero else the fifo never clears
        lcd_print_values(average, std_deviation, cnt); // print values
    }

    // if there are updated values
    if(flag_send){
        uart_print_values(cnt, t_diff); // print the total count and the time difference between grains to uart
        flag_send = false; // reset send flag
        push_fifo(t_diff); // update list for avg
    }
}
