/**
 *
 * Arduino code for controlling a gamecube playing Dr. Robotnik's Mean Bean
 * Machine.
 *
 * See `README.md` for info on setup.
 *
 * ========= Resources =========
 *
 * An Arduino based Gamecube to N64 controller converter. The assembly
 * routines in this file are taken from the Gamecube to N64 converter.
 * https://github.com/brownan/Gamecube-N64-Controller
 *
 * A lot of details on the protocol:
 * http://www.int03.co.uk/crema/hardware/gamecube/gc-control.htm
 *
 * An old archived page with a few details about the gamecube protocol:
 * https://web.archive.org/web/20071025150150/http://www.gc-linux.org/docs/yagcd/chap9.html
 *
 */

#include "pins_arduino.h"



// 8 bytes of data that we send to the GC, in packed format ready to push onto
// the wire.
typedef struct {
    // bits: 0, 0, 0, start, y, x, b, a
    unsigned char data1;
    // bits: 1, L, R, Z, Dup, Ddown, Dright, Dleft
    unsigned char data2;
    unsigned char stick_x;  // 128 is center for stick and cstick
    unsigned char stick_y;
    unsigned char cstick_x;
    unsigned char cstick_y;
    unsigned char left;  // left/right trigger analog
    unsigned char right;
} GCStatus;

static const GCStatus initial_gc_status = {0, 0, 128, 128, 128, 128, 0, 0};

static GCStatus gc_status = initial_gc_status;

// Response to the identify (0x00) command
// Some of the meaning of this response is described here:
//   https://web.archive.org/web/20071025150150/http://www.gc-linux.org/docs/yagcd/chap9.html
static unsigned char identify_response[] = {0x09, 0x00, 0x20};

// Response to the origins (0x41) command
// Not sure what these bits mean, but this is a response I sniffed from my
// controller.
static unsigned char origin_response[] = {
    0x00, 0x80, 0x7d, 0x87, 0x82,
    0x85, 0x1f, 0x22, 0x00, 0x00
};

static const int MOVE_QUEUE_MAX = 10;
typedef struct {
    struct {
        GCStatus status;
        unsigned int count;
    } queue[MOVE_QUEUE_MAX];
    int n;
    int pos;
} MoveQueue;

static MoveQueue move_queue;

static void MoveQueue_clear(MoveQueue* mq);
static void MoveQueue_add_move(MoveQueue* mq, const GCStatus* status, unsigned int count);
static const GCStatus* MoveQueue_get_status(const MoveQueue* mq);
static const GCStatus* MoveQueue_next(MoveQueue* mq);


void setup() {

    // Make sure the data pins stay LOW and in INPUT mode when idle.
    // If you change this, make sure to change the pins the assembly uses too.
    digitalWrite(2, LOW);
    pinMode(2, INPUT);

    Serial.begin(115200);

    MoveQueue_clear(&move_queue);

}

unsigned int i=0;
void loop() {
    int ret;
    unsigned char from_gc;

    do {
        ret = gc_get(&from_gc);
    } while (ret != 1);

    switch(from_gc) {

        case 0x00:  // Identify Command
            gc_send(identify_response, sizeof(identify_response));
        break;

        case 0x41:  // Origin Command
            delayMicroseconds(4);  // Not sure why this is needed
            gc_send(origin_response, sizeof(origin_response));
        break;

        case 0x40:  // Read Command

            // Delay until the gamecube is listening
            // We are receiving 24 bits plus a stop bit, so 25 bits total, but
            // we've already received 8 of them. We must wait until the
            // gamecube is done sending. Each bit takes 4 microseconds, so
            // we'll wait 4*17 plus a little extra to give the gamecube a
            // chance to wait for our response.
            delayMicroseconds(4*17 + 28);

            gc_send((unsigned char*) &gc_status, sizeof(gc_status));
        break;

        //default:  // Unknown
        //    Serial.println(from_gc, HEX);

    }

    if(Serial.available()) {
        GCStatus tmp_status = initial_gc_status;

        unsigned char b = Serial.read();
        int delta_x = ((b & 0x38) >> 3) - 2;  // Amount to move left/right
        int rot = (b & 0x06) >> 1;  // # clockwise rotations
        bool down_fast = (b & 0x01);  // Drop piece quickly?

        MoveQueue_clear(&move_queue);

        do {

            if(delta_x > 0) {
                tmp_status.stick_x = 255;
                delta_x--;
            } else if(delta_x < 0) {
                tmp_status.stick_x = 0;
                delta_x++;
            }

            switch(rot) {
                case 1:
                case 2:
                    tmp_status.data1 |= 0x04;  // Press X, rotate clockwise
                    rot--;
                break;
                case 3:
                    tmp_status.data1 |= 0x01;  // Press A, rotate anticlockwise
                    rot = 0;
                break;
            }

            MoveQueue_add_move(&move_queue, &tmp_status, 1);

            // Clear controller
            tmp_status = initial_gc_status;
            if(delta_x != 0 || rot != 0) {
                MoveQueue_add_move(&move_queue, &tmp_status, 1);
            }

        } while(delta_x != 0 || rot != 0);

        if(down_fast) {
            tmp_status.stick_y = 0;
            MoveQueue_add_move(&move_queue, &tmp_status, 8);
        }

        // Clear controller one last time
        tmp_status = initial_gc_status;
        MoveQueue_add_move(&move_queue, &tmp_status, 1);

        // Reset counter and get the first status from the queue
        i = 0;
        gc_status = *MoveQueue_get_status(&move_queue);

    // Get next gc_status from move_queue every once in a while
    } else if(i % 5 == 0) {
        gc_status = *MoveQueue_next(&move_queue);
    }

    i++;
}

static void MoveQueue_clear(MoveQueue* mq) {
    mq->n = 0;
    mq->pos = 0;
    mq->queue[0].status = initial_gc_status;
}

static void MoveQueue_add_move(MoveQueue* mq, const GCStatus* status, unsigned int count) {
    if(mq->n >= MOVE_QUEUE_MAX) {
        return;
    }
    memcpy((void*) &mq->queue[mq->n].status, (void*) status, sizeof(GCStatus));
    mq->queue[mq->n].count = count;
    mq->n++;
}

static const GCStatus* MoveQueue_get_status(const MoveQueue* mq) {
    if(mq->pos < 0 || mq->pos >= mq->n) {
        return &initial_gc_status;
    }
    return &mq->queue[mq->pos].status;
}

static const GCStatus* MoveQueue_next(MoveQueue* mq) {
    if(mq->pos >= mq->n) {
        MoveQueue_clear(mq);
    } else {
        mq->queue[mq->pos].count--;
        if(mq->queue[mq->pos].count == 0) {
            mq->pos++;
        }
    }
    const GCStatus* status = MoveQueue_get_status(mq);
    return status;
}

/**
 * This sends the given byte sequence to the gamecube
 * length must be at least 1
 * hardcoded for Arduino DIO 2 and external pull-up resistor (sets line to
 * OUTPUT for low and INPUT for high)
 */
static void gc_send(unsigned char *buffer, char length)
{
    noInterrupts();
    asm volatile (
        "; Start of gc_send assembly\n"

        // passed in to this block are:
        // the Z register (r31:r30) is the buffer pointer
        // %[length] is the register holding the length of the buffer in bytes

        // Instruction cycles are noted in parentheses
        // branch instructions have two values, one if the branch isn't
        // taken and one if it is

        // r25 will be the current buffer byte loaded from memory
        // r26 will be the bit counter for the current byte. when this
        // reaches 0, we need to decrement the length counter, load
        // the next buffer byte, and loop. (if the length counter becomes
        // 0, that's our exit condition)

        "ld r25, Z\n" // load the first byte

        // This label starts the outer loop, which sends a single byte
        ".L%=_byte_loop:\n"
        "ldi r26,lo8(8)\n" // (1)

        // This label starts the inner loop, which sends a single bit
        ".L%=_bit_loop:\n"
        "sbi 0xa,2\n" // (2) pull the line low

        // line needs to stay low for 1µs for a 1 bit, 3µs for a 0 bit
        // this block figures out if the next bit is a 0 or a 1
        // the strategy here is to shift the register left, then test and
        // branch on the carry flag
        "lsl r25\n" // (1) shift left. MSB goes into carry bit of status reg
        "brcc .L%=_zero_bit\n" // (1/2) branch if carry is cleared


        // this block is the timing for a 1 bit (1µs low, 3µs high)
        // Stay low for 16 - 2 (above lsl,brcc) - 2 (below cbi) = 12 cycles
        "nop\nnop\nnop\nnop\nnop\n" // (5)
        "nop\nnop\nnop\nnop\nnop\n" // (5)
        "nop\nnop\n" // (2)
        "cbi 0xa,2\n" // (2) set the line high again
        // Now stay high for 2µs of the 3µs to sync up with the branch below
        // 2*16 - 2 (for the rjmp) = 30 cycles
        "nop\nnop\nnop\nnop\nnop\n" // (5)
        "nop\nnop\nnop\nnop\nnop\n" // (5)
        "nop\nnop\nnop\nnop\nnop\n" // (5)
        "nop\nnop\nnop\nnop\nnop\n" // (5)
        "nop\nnop\nnop\nnop\nnop\n" // (5)
        "nop\nnop\nnop\nnop\nnop\n" // (5)
        "rjmp .L%=_finish_bit\n" // (2)


        // this block is the timing for a 0 bit (3µs low, 1µs high)
        // Need to go high in 3*16 - 3 (above lsl,brcc) - 2 (below cbi) = 43 cycles
        ".L%=_zero_bit:\n"
        "nop\nnop\nnop\nnop\nnop\n" // (5)
        "nop\nnop\nnop\nnop\nnop\n" // (5)
        "nop\nnop\nnop\nnop\nnop\n" // (5)
        "nop\nnop\nnop\nnop\nnop\n" // (5)
        "nop\nnop\nnop\nnop\nnop\n" // (5)
        "nop\nnop\nnop\nnop\nnop\n" // (5)
        "nop\nnop\nnop\nnop\nnop\n" // (5)
        "nop\nnop\nnop\nnop\nnop\n" // (5)
        "nop\nnop\nnop\n" // (3)
        "cbi 0xa,2\n" // (2) set the line high again


        // The two branches meet up here.
        // We are now *exactly* 3µs into the sending of a bit, and the line
        // is high again. We have 1µs to do the looping and iteration
        // logic.
        ".L%=_finish_bit:\n"
        "subi r26,1\n" // (1) subtract 1 from our bit counter
        "breq .L%=_load_next_byte\n" // (1/2) branch if we've sent all the bits of this byte

        // At this point, we have more bits to send in this byte, but the
        // line must remain high for another 1µs (minus the above
        // instructions and the jump below and the sbi instruction at the
        // top of the loop)
        // 16 - 2(above) - 2 (rjmp below) - 2 (sbi after jump) = 10
        "nop\nnop\nnop\nnop\nnop\n" // (5)
        "nop\nnop\nnop\nnop\nnop\n" // (5)
        "rjmp .L%=_bit_loop\n"


        // This block starts 3 cycles into the last 1µs of the line being high
        // We need to decrement the byte counter. If it's 0, that's our exit condition.
        // If not we need to load the next byte and go to the top of the byte loop
        ".L%=_load_next_byte:\n"
        "subi %[length], 1\n" // (1)
        "breq .L%=_loop_exit\n" // (1/2) if the byte counter is 0, exit
        "adiw r30,1\n" // (2) increment byte pointer
        "ld r25, Z\n" // (2) load the next byte
        // delay block:
        // needs to go high after 1µs or 16 cycles
        // 16 - 9 (above) - 2 (the jump itself) - 3 (after jump) = 2
        "nop\nnop\n" // (2)
        "rjmp .L%=_byte_loop\n" // (2)


        // Loop exit
        ".L%=_loop_exit:\n"

        // final task: send the stop bit, which is a 1 (1µs low 3µs high)
        // the line goes low in:
        // 16 - 6 (above since line went high) - 2 (sbi instruction below) = 8 cycles
        "nop\nnop\nnop\nnop\nnop\n" // (5)
        "nop\nnop\nnop\n" // (3)
        "sbi 0xa,2\n" // (2) pull the line low
        // stay low for 1µs
        // 16 - 2 (below cbi) = 14
        "nop\nnop\nnop\nnop\nnop\n" // (5)
        "nop\nnop\nnop\nnop\nnop\n" // (5)
        "nop\nnop\nnop\nnop\n" // (4)
        "cbi 0xa,2\n" // (2) set the line high again

        // just stay high. no need to wait 3µs before returning

        :
        // outputs:
        "+z" (buffer) // (read and write)
        :
        // inputs:
        [length] "d" (length)
        :
        // clobbers:
            "r25", "r26"
    );

    interrupts();
}

// Read bytes from the gamecube
// hardwired to read from Arduino DIO2 with external pullup resistor
// returns 1 for success, 0 for failure
static int gc_get(unsigned char *bitbin)
{
    noInterrupts();
    // listen data from the GC and stuff it into the given pointer.
    asm volatile (";Starting to listen");

    unsigned char retval;

    asm volatile (
            "; START OF MANUAL ASSEMBLY BLOCK\n"
            // r25 is our bit counter. We read 24 bits and increment the byte
            // pointer every 8 bits
            "ldi r25,lo8(0)\n"
            // default exit value is 1 (success)
            "ldi %[retval],lo8(1)\n"

            // Top of the main read loop label
            "L%=_read_loop:\n"

            // This first spinloop waits for the line to go low. It loops
            // until it gives up and returns
            "ldi r24,lo8(64)\n" // r24 is the timeout counter
            "L%=_1:\n"
            "sbis 0x9,2\n" // reg 9 bit 2 is PIND2, or arduino I/O 2
            "rjmp L%=_2\n" // line is low. jump to below
            // the following happens if the line is still high
            "subi r24,lo8(1)\n"
            "brne L%=_1\n" // loop if the counter isn't 0
            "jmp L%=_1\n" // Uncomment this to loop indefinitely
            // timeout? set output to 0 indicating failure and jump to
            // the end
            "ldi %[retval],lo8(0)\n"
            "rjmp L%=_exit\n"
            "L%=_2:\n"

            // Next block. The line has just gone low. Wait approx 2µs
            // each cycle is 1/16 µs on a 16Mhz processor
            "nop\nnop\nnop\nnop\nnop\n"
            "nop\nnop\nnop\nnop\nnop\n"
            "nop\nnop\nnop\nnop\nnop\n"
            "nop\nnop\nnop\nnop\nnop\n"
            "nop\nnop\nnop\nnop\nnop\n"
            "nop\nnop\nnop\nnop\nnop\n"

            // This block left shifts the current buffer byte in r23,
            // and adds the current line state as the LSB
            "lsl r23\n" // left shift
            "sbic 0x9,2\n" // read PIND2
            "sbr r23,lo8(1)\n" // set bit 1 in r23 if PIND2 is high
            "st Z,r23\n" // save r23 back to memory. We technically only have
            // to do this every 8 bits but this simplifies the branches below

            // This block increments the bitcount(r25). If we've read enough
            // bits, exit with success. If bitcount is a multiple of 8, then
            // increment Z and load the next byte.
            "subi r25,lo8(-1)\n" // increment bitcount
            "cpi r25,lo8(8)\n" // read enough bits yet?
            "breq L%=_exit\n" // jump to exit
            "mov r24,r25\n" // copy bitcounter(r25) to r24 for tmp
            "andi r24,lo8(7)\n" // get lower 3 bits
            "brne L%=_3\n" // branch if not 0 (is not divisble by 8)
            "adiw r30,1\n" // if divisible by 8, increment pointer
            "ld r23,Z\n" // ...and load the new byte into r23
            "L%=_3:\n"

            // This next block waits for the line to go high again. It sets a
            // timeout counter of 64 iterations
            "ldi r24,lo8(64)\n" // r24 is the timeout counter
            "L%=_4:\n"
            "sbic 0x9,2\n" // checks PIND2
            "rjmp L%=_read_loop\n" // line is high. ready for next loop
            // the following happens if the line is still low
            "subi r24,lo8(1)\n"
            "brne L%=_4\n" // loop if the counter isn't 0
            // timeout? set output to 0 indicating failure and fall through to
            // the end
            "ldi %[retval],lo8(0)\n"

            "L%=_exit:\n"
            ";END OF MANUAL ASSEMBLY BLOCK\n"
            // ----------
            // outputs:
            : [retval] "=r" (retval),
            // About the bitbin pointer: The "z" constraint tells the
            // compiler to put the pointer in the Z register pair (r31:r30)
            // The + tells the compiler that we are both reading and writing
            // this pointer. This is important because otherwise it will
            // allocate the same register for retval (r30).
            "+z" (bitbin)
            // inputs:
            :
            // clobbers (registers we use in the assembly for the compiler to
            // avoid):
            : "r25", "r24", "r23", "r22"
            );

    interrupts();
    return retval;
}
