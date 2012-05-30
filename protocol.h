#ifndef __PROTOCOL_H__
#define __PROTOCOL_H__

#include <stdint.h>
/**
 * 定义缓存系统和客户端, 从节点, 数据库通信的协议
 *
 * */

// packet
// head+extas+key+value

/* 头部定义 */
// version + headlen + opcode + bodylen + reserve
//

#define PROTOCOL_VERSION 1

#pragma pack(1)

/* 请求包头部 */
typedef union {
	struct {
		uint8_t  version;
		uint8_t  opcode;
		uint16_t reserve;
		uint32_t bodylen;
		uint32_t kvcount;
	};
	char bytes[12];
}request_header;

/* 回应包头部 */
typedef union {
	struct {
		uint8_t  version;
		uint8_t  opcode;
		uint16_t status;
		uint32_t bodylen;
		uint32_t kvcount;
	};
	char bytes[12];
}response_header;

/* 包结构定义 */
// body = kvcount*{klen+key+cas+vlen+value}
/*
 *  uint16_t klen
 *  char *key
 *  uint64_t cas
 *  uint32_t vlen
 *  void *value
 *
 */
/*typedef struct{
	request_header head;
	char *body;
	char *key;
	char *value;
}request_packet;
*/

#pragma pack()

/* 协议版本号 */
#define protocol_version 1

/* 返回包中的状态(status) */
typedef enum{
		RESPONSE_SUCCESS = 0x00,
		RESPONSE_SOMEKEY_NOTFOUND = 0x01,
		RESPONSE_SOMEKEY_EXIST = 0x02,
		RESPONSE_PKT_2BIG = 0x03,
		RESPONSE_ARG_INVALID = 0x04,
		RESPONSE_UNKNOW_CMD = 0x05,
		RESPONSE_OOM = 0x06,
		RESPONSE_INTERNAL_ERR = 0x07,
		RESPONSE_BUSY = 0x08,
		RESPONSE_VALUE_2BIG = 0x09,
		RESPONSE_REPLY_PKT_2BIG = 0x0a
}PKT_STATUS;

/* 可能的系统处理命令 */
typedef enum{
	PKT_CMD_GET = 0x00,
	PKT_CMD_SET = 0x01,
	PKT_CMD_ADD = 0x02,
	PKT_CMD_REPLACE = 0x03,
	PKT_CMD_DELETE = 0x04,
	PKT_CMD_QUIT = 0x05,
	PKT_CMD_VERSION = 0x06,
	PKT_CMD_STAT = 0x07,
	PKT_CMD_SYNC = 0x08
}PKT_OPCMD;

/* 键值中的值类型 */
typedef enum{
	DATA_RAW_BYTES = 0X00
}PKT_DATATYPE;

#endif // __PROTOCOL_H__
