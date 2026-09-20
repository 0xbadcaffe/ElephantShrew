#include "elephantshrew_shared.h"

int main(void) {
    return ES_ABI_VERSION == 2u ? 0 : 1;
}