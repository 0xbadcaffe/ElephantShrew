#include "elephantshrew_shared.h"
#include <type_traits>

static_assert(std::is_trivially_copyable_v<es_event>);
static_assert(std::is_standard_layout_v<es_event>);

int main() {
    return 0;
}