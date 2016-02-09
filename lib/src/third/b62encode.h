#ifndef b62_encode_h
#define b62_encode_h
/** encode an array of bytes data of length into out, which the user
must size large enough (approximately 1.4*length).
*/
extern int b62_encode(char* out, const unsigned char* data, int length);
#endif
