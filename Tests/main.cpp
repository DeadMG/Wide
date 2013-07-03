#define CATCH_CONFIG_MAIN
#include "catch.h"

TEST_CASE( "The simplest shit ever", "[simple]" ) {
    REQUIRE( 1 == 1 );
}