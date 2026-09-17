#include <mycelius/ipc.h>
/* Example EL0 CLI service. It has no access to EL1 or another app's pages. */
void app_main(void) { struct mycelius_message event; for (;;) if (mycelius_receive(&event) == 0) { (void)event; } }
