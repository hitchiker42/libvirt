/*
 * job_control.c Core implementation of job control
 *
 * Copyright (C) 2014 Tucker DiNapoli
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * Author: Tucker DiNapoli
 */

#include <config.h>

#include <stdlib.h>
#include <time.h>
#include <sys/time.h>

#define timevalToMS(timeval) ((long)(timeval.tv_sec*1000)+(long)(timeval.tv_usec/1000))

#define MStoTimeval(MS) (struct timeval){.tv_sec=(MS/1000),.tv_usec=(MS%1000)}

#define timevalSub(tv1,tv2) (struct timeval){.tv_sec=(tv1.tv_sec-tv2.tv_sec \
                                                      .tv_usec=(tv1.tv_usec-tv2.tv_usec)}
/*
  To get a rate in %/sec do
  double rate=(double)(percent_complete-percent_complete_last)/(timediff/1000.0)
  rate=(2*rate+old_rate)/3;//average rate with old rate for consistancy
  //multiple old rates could be kept to further help keeping the rate stable
*/
