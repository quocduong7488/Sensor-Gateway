#ifndef STORAGEMGR_H
#define STORAGEMGR_H

#include <sqlite3.h>

#include "gateway.h"

/*
 * Tên file SQLite database.
 */
#define STORAGE_DB_FILE "sensor_data.db"

/*
 * Tên bảng chứa dữ liệu sensor.
 */
#define STORAGE_TABLE_NAME "SensorData"

/*
 * Số lần thử kết nối lại database tối đa.
 */
#define STORAGE_MAX_RETRIES 3

/*
 * Thời gian chờ giữa hai lần retry, tính bằng giây.
 */
#define STORAGE_RETRY_DELAY 1

/*
 * Storage Manager thread entry point.
 */
void *storagemgr(void *arg);

#endif
