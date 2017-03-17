# minimal cmake file for jzon

SET(SOURCES jzon.c jzon.h)
SOURCE_GROUP(jzon FILES ${SOURCES})
add_library(jzon STATIC ${SOURCES})