/*
 * ElephantShrewCommonDefs.h
 *
 *  Created on: 3 Jun 2022
 *      Author: misteroy
 */

#ifndef ELEPHENTSHREW_COMMONDEFS_H_
#define ELEPHENTSHREW_COMMONDEFS_H_


namespace ElephantShrew {
/*
 * ElephantShrew bootstrapper configuration
 */

/* Only one transmission method can be turned on */
constexpr auto USB_TRANSMIT = true;
constexpr auto UDP_TRANSMIT = false;
/* Only one receive method can be turned on */
constexpr auto FILE_RECEIVE = true;
constexpr auto UDP_RECEIVE  = false;
/*
 * ElephantShrew input date configuration
 */
constexpr auto ElephantShrew_LINE_SIZE = 10;
constexpr auto ElephantShrew_MAX_COUNTS = 1000;


}



#endif /* ELEPHENTSHREW_COMMONDEFS_H_ */
