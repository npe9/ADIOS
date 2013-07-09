/**
 * misc.h
 *
 *  Created on: Jul 5, 2013
 *  Author: Magda Slawinska aka Magic Magg magg dot gatech at gmail.com
 */

#ifndef MISC_H_
#define MISC_H_

//! if defined (not commented) the flexpath method will
//! be used; otherwise the  ADIOS_READ_METHOD_BP and MPI
//! if you are switching methods, make sure that test_config.xml
//! contains the correct method
#define FLEXPATH_METHOD 1

//! the name of the file to be written test values
#define FILE_NAME "test.bp"

//! the xml containing configuration of ADIOS
#define XML_ADIOS_INIT_FILENAME "test_config.xml"

//! options how verbose is ADIOS (see  adios_read_init_method)
//! 0=quiet, ..., 4=debug
#define ADIOS_OPTIONS "verbose=4; show hidden_attrs"

//! defines if the test passed
#define TEST_PASSED 0
//! defines if the test failed
#define TEST_FAILED -1


#endif /* MISC_H_ */
