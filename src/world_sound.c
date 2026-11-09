/* World V35, physical 00713. DH selects the YM2151 register, DL its data.
 * SS is the audio driver's I/O segment. The interrupt wrapper saves flags
 * and excludes interrupts through both busy waits and writes. */
typedef unsigned char Byte;
typedef unsigned short Word;

static Byte fm_status(void);
#pragma aux fm_status = "mov al,ss:[8042h]" value [al] modify exact [al]
static void select_register(Word command);
#pragma aux select_register = "mov ss:[8040h],dh" parm [dx] modify exact []
static void write_data(Word command);
#pragma aux write_data = "mov ss:[8042h],dl" parm [dx] modify exact []

void write_fm_register(Word command);
#pragma aux write_fm_register parm [dx] modify exact [ax]
void write_fm_register(Word command)
{
    while (fm_status() & 0x80) { }
    select_register(command);
    while (fm_status() & 0x80) { }
    write_data(command);
}
