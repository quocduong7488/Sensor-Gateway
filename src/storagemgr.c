#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <sqlite3.h>
#include <stdio.h>
#include <time.h>

#include "storagemgr.h"
#include "logger.h"
#include "sbuffer.h"

/*
 * SQL tạo bảng SensorData.
 */
static const char *CREATE_TABLE_SQL =
    "CREATE TABLE IF NOT EXISTS SensorData ("
    "id INTEGER NOT NULL,"
    "type INTEGER NOT NULL,"
    "value REAL NOT NULL,"
    "ts INTEGER NOT NULL"
    ");";

/*
 * SQL insert một measurement.
 */
static const char *INSERT_SQL =
    "INSERT INTO SensorData (id, type, value, ts) "
    "VALUES (?, ?, ?, ?);";

/*
 * Ghi log của Storage Manager.
 */
static void storage_log(const char *message)
{
    (void)logger_write("STOR", message);
}

/*
 * Chờ một khoảng thời gian trước khi retry.
 */
static void storage_retry_delay(void)
{
    struct timespec delay;

    delay.tv_sec = STORAGE_RETRY_DELAY;
    delay.tv_nsec = 0;

    while (nanosleep(&delay, &delay) != 0) {
        if (errno != EINTR) {
            break;
        }
    }
}

/*
 * Mở SQLite database.
 *
 * Trả về:
 *   SQLITE_OK : thành công
 *   mã lỗi    : thất bại
 */
static int storage_open_database(sqlite3 **db)
{
    int rc;

    if (db == NULL) {
        return SQLITE_MISUSE;
    }

    *db = NULL;

    rc = sqlite3_open(STORAGE_DB_FILE, db);

    if (rc != SQLITE_OK) {
        if (*db != NULL) {
            sqlite3_close(*db);
            *db = NULL;
        }

        return rc;
    }

    /*
     * Bật foreign key support.
     * Hiện tại SensorData chưa sử dụng foreign key,
     * nhưng việc bật pragma không ảnh hưởng schema hiện tại.
     */
    rc = sqlite3_exec(
        *db,
        "PRAGMA foreign_keys = ON;",
        NULL,
        NULL,
        NULL
    );

    if (rc != SQLITE_OK) {
        sqlite3_close(*db);
        *db = NULL;
        return rc;
    }

    return SQLITE_OK;
}

/*
 * Đóng database và giải phóng prepared statement.
 */
static void storage_close_database(
    sqlite3 *db,
    sqlite3_stmt *statement
)
{
    if (statement != NULL) {
        sqlite3_finalize(statement);
    }

    if (db != NULL) {
        sqlite3_close(db);
    }
}

/*
 * Tạo bảng SensorData nếu chưa tồn tại.
 *
 * Chỉ ghi:
 *
 *     New table SensorData created.
 *
 * khi bảng thực sự chưa tồn tại trước đó.
 */
static int storage_create_table(sqlite3 *db)
{
    sqlite3_stmt *statement = NULL;
    char *error_message = NULL;

    int rc;
    int table_exists = 0;

    if (db == NULL) {
        return SQLITE_MISUSE;
    }

    /*
     * Kiểm tra bảng SensorData đã tồn tại hay chưa.
     */
    rc = sqlite3_prepare_v2(
        db,
        "SELECT 1 FROM sqlite_master "
        "WHERE type = 'table' AND name = ?;",
        -1,
        &statement,
        NULL
    );

    if (rc != SQLITE_OK) {
        return rc;
    }

    rc = sqlite3_bind_text(
        statement,
        1,
        STORAGE_TABLE_NAME,
        -1,
        SQLITE_STATIC
    );

    if (rc != SQLITE_OK) {
        sqlite3_finalize(statement);
        return rc;
    }

    rc = sqlite3_step(statement);

    if (rc == SQLITE_ROW) {
        table_exists = 1;
    } else if (rc != SQLITE_DONE) {
        sqlite3_finalize(statement);
        return rc;
    }

    sqlite3_finalize(statement);
    statement = NULL;

    /*
     * Bảng đã tồn tại.
     *
     * Không cần tạo lại và không ghi log "New table".
     */
    if (table_exists) {
        return SQLITE_OK;
    }

    /*
     * Bảng chưa tồn tại -> tạo bảng.
     */
    rc = sqlite3_exec(
        db,
        CREATE_TABLE_SQL,
        NULL,
        NULL,
        &error_message
    );

    if (rc != SQLITE_OK) {
        if (error_message != NULL) {
            fprintf(
                stderr,
                "Storage Manager: create table failed: %s\n",
                error_message
            );

            sqlite3_free(error_message);
        }

        return rc;
    }

    storage_log("New table SensorData created.");

    return SQLITE_OK;
}

/*
 * Chuẩn bị INSERT statement.
 */
static int storage_prepare_insert(
    sqlite3 *db,
    sqlite3_stmt **statement
)
{
    if (db == NULL || statement == NULL) {
        return SQLITE_MISUSE;
    }

    *statement = NULL;

    return sqlite3_prepare_v2(
        db,
        INSERT_SQL,
        -1,
        statement,
        NULL
    );
}

/*
 * Khởi tạo hoàn chỉnh một kết nối Storage Manager.
 *
 * Bao gồm:
 *
 *   1. Mở database
 *   2. Tạo SensorData nếu chưa tồn tại
 *   3. Chuẩn bị INSERT statement
 *
 * Nếu bất kỳ bước nào thất bại, toàn bộ tài nguyên
 * của lần khởi tạo này được giải phóng.
 *
 * Caller chịu trách nhiệm retry.
 */
static int storage_initialize(
    sqlite3 **db,
    sqlite3_stmt **statement
)
{
    int rc;

    if (db == NULL || statement == NULL) {
        return SQLITE_MISUSE;
    }

    *db = NULL;
    *statement = NULL;

    /*
     * Bước 1: mở database.
     */
    rc = storage_open_database(db);

    if (rc != SQLITE_OK) {
        return rc;
    }

    /*
     * Bước 2: tạo bảng nếu cần.
     */
    rc = storage_create_table(*db);

    if (rc != SQLITE_OK) {
        storage_close_database(*db, NULL);
        *db = NULL;

        return rc;
    }

    /*
     * Bước 3: chuẩn bị INSERT statement.
     */
    rc = storage_prepare_insert(
        *db,
        statement
    );

    if (rc != SQLITE_OK) {
        storage_close_database(
            *db,
            *statement
        );

        *db = NULL;
        *statement = NULL;

        return rc;
    }

    return SQLITE_OK;
}

/*
 * Bind sensor_data_t vào prepared statement.
 */
static int storage_bind_data(
    sqlite3_stmt *statement,
    const sensor_data_t *data
)
{
    int rc;

    if (statement == NULL || data == NULL) {
        return SQLITE_MISUSE;
    }

    /*
     * id
     */
    rc = sqlite3_bind_int(
        statement,
        1,
        (int)data->id
    );

    if (rc != SQLITE_OK) {
        return rc;
    }

    /*
     * type
     */
    rc = sqlite3_bind_int(
        statement,
        2,
        (int)data->type
    );

    if (rc != SQLITE_OK) {
        return rc;
    }

    /*
     * value
     */
    rc = sqlite3_bind_double(
        statement,
        3,
        data->value
    );

    if (rc != SQLITE_OK) {
        return rc;
    }

    /*
     * timestamp.
     */
    rc = sqlite3_bind_int64(
        statement,
        4,
        (sqlite3_int64)data->ts
    );

    return rc;
}

/*
 * INSERT một measurement vào SQLite.
 *
 * Trả về SQLITE_OK khi INSERT thành công.
 *
 * Measurement chỉ được ACK trong sbuffer sau khi hàm này
 * trả về SQLITE_OK.
 */
static int storage_insert(
    sqlite3_stmt *statement,
    const sensor_data_t *data
)
{
    int rc;

    if (statement == NULL || data == NULL) {
        return SQLITE_MISUSE;
    }

    /*
     * Reset statement trước khi sử dụng lại.
     */
    rc = sqlite3_reset(statement);

    if (rc != SQLITE_OK) {
        return rc;
    }

    /*
     * Xóa binding của lần INSERT trước.
     */
    rc = sqlite3_clear_bindings(statement);

    if (rc != SQLITE_OK) {
        return rc;
    }

    /*
     * Bind measurement.
     */
    rc = storage_bind_data(
        statement,
        data
    );

    if (rc != SQLITE_OK) {
        return rc;
    }

    /*
     * Thực hiện INSERT.
     */
    rc = sqlite3_step(statement);

    /*
     * INSERT thành công phải trả SQLITE_DONE.
     */
    if (rc != SQLITE_DONE) {
        return rc;
    }

    /*
     * Reset để statement có thể được sử dụng lại.
     */
    rc = sqlite3_reset(statement);

    if (rc != SQLITE_OK) {
        return rc;
    }

    return SQLITE_OK;
}

/*
 * Thử khởi tạo/reconnect Storage Manager tối đa 3 lần.
 *
 * Khi thành công:
 *   db                -> database connection
 *   insert_statement  -> prepared INSERT statement
 *
 * Khi thất bại:
 *   db                = NULL
 *   insert_statement  = NULL
 */
static int storage_connect_with_retry(
    sqlite3 **db,
    sqlite3_stmt **insert_statement,
    gateway_t *gateway
)
{
    int rc;
    int attempt;

    if (db == NULL ||
        insert_statement == NULL ||
        gateway == NULL) {
        return SQLITE_MISUSE;
    }

    *db = NULL;
    *insert_statement = NULL;

    for (attempt = 1;
         attempt <= STORAGE_MAX_RETRIES;
         ++attempt) {

        /*
         * Nếu Gateway đang shutdown thì không retry nữa.
         */
        if (atomic_load(&gateway->stop)) {
            return SQLITE_ABORT;
        }

        rc = storage_initialize(
            db,
            insert_statement
        );

        if (rc == SQLITE_OK) {
            storage_log(
                "Connection to SQL server established."
            );

            return SQLITE_OK;
        }

        /*
         * Đảm bảo không để lại tài nguyên từ lần thử thất bại.
         */
        storage_close_database(
            *db,
            *insert_statement
        );

        *db = NULL;
        *insert_statement = NULL;

        storage_log(
            "Unable to connect to SQL server."
        );

        if (attempt < STORAGE_MAX_RETRIES) {
            storage_retry_delay();
        }
    }

    return SQLITE_ERROR;
}

/*
 * Storage Manager thread.
 */
void *storagemgr(void *arg)
{
    gateway_t *gateway;
    sqlite3 *db = NULL;
    sqlite3_stmt *insert_statement = NULL;

    int rc;

    gateway = (gateway_t *)arg;

    if (gateway == NULL || gateway->sbuffer == NULL) {
        return NULL;
    }

    /*
     * ============================================================
     * PHẦN 1: KHỞI TẠO DATABASE
     * ============================================================
     *
     * Bao gồm:
     *
     *   - sqlite3_open()
     *   - CREATE TABLE IF NOT EXISTS
     *   - sqlite3_prepare_v2()
     *
     * Toàn bộ quá trình được retry tối đa 3 lần.
     */
    rc = storage_connect_with_retry(
        &db,
        &insert_statement,
        gateway
    );

    if (rc != SQLITE_OK) {
        /*
         * Không thể kết nối/khởi tạo database sau 3 lần.
         *
         * Yêu cầu Step 6:
         * Gateway phải terminate.
         */
        atomic_store(&gateway->stop, true);
        sbuffer_shutdown(gateway->sbuffer);

        storage_close_database(
            db,
            insert_statement
        );

        return NULL;
    }

    /*
     * ============================================================
     * PHẦN 2: ĐỌC DỮ LIỆU TỪ SBUFFER VÀ INSERT
     * ============================================================
     */
    for (;;) {
        sensor_data_t data;
        sbuffer_node_t *token = NULL;

        /*
         * Nếu Gateway đang shutdown thì kết thúc thread.
         */
        if (atomic_load(&gateway->stop)) {
            break;
        }

        /*
         * Lấy measurement nhưng CHƯA đánh dấu consumed.
         */
        rc = sbuffer_storage_peek(
            gateway->sbuffer,
            &data,
            &token
        );

        if (rc == 0) {
            /*
             * sbuffer đã shutdown.
             */
            break;
        }

        if (rc < 0) {
            /*
             * Lỗi nội bộ sbuffer.
             */
            atomic_store(&gateway->stop, true);
            sbuffer_shutdown(gateway->sbuffer);
            break;
        }

        /*
         * ========================================================
         * INSERT VÀO SQLITE
         * ========================================================
         */
        rc = storage_insert(
            insert_statement,
            &data
        );

        if (rc == SQLITE_OK) {
            /*
             * INSERT thành công.
             *
             * Chỉ lúc này mới ACK node trong sbuffer.
             */
            if (sbuffer_storage_ack(
                    gateway->sbuffer,
                    token) != 0) {

                atomic_store(&gateway->stop, true);
                sbuffer_shutdown(gateway->sbuffer);
                break;
            }

            continue;
        }

        /*
         * ========================================================
         * INSERT THẤT BẠI
         * ========================================================
         *
         * KHÔNG gọi sbuffer_storage_ack().
         *
         * Measurement vẫn còn trong sbuffer.
         */
        storage_log(
            "Connection to SQL server lost."
        );

        /*
         * Đóng connection và prepared statement cũ.
         */
        storage_close_database(
            db,
            insert_statement
        );

        db = NULL;
        insert_statement = NULL;

        /*
         * ========================================================
         * RECONNECT DATABASE
         * ========================================================
         *
         * Measurement hiện tại vẫn chưa ACK.
         *
         * Nếu reconnect thành công, vòng for sẽ quay lại
         * sbuffer_storage_peek() và lấy lại chính measurement này.
         */
        rc = storage_connect_with_retry(
            &db,
            &insert_statement,
            gateway
        );

        if (rc != SQLITE_OK) {
            /*
             * Sau 3 lần retry vẫn không khôi phục được DB.
             *
             * Gateway terminate.
             */
            atomic_store(&gateway->stop, true);
            sbuffer_shutdown(gateway->sbuffer);
            break;
        }

        /*
         * Không ACK measurement cũ ở đây.
         *
         * Vòng lặp quay lại sbuffer_storage_peek().
         * Vì read_by_storage vẫn là false, node cũ sẽ
         * được lấy lại và INSERT lại.
         */
    }

    /*
     * ============================================================
     * CLEANUP
     * ============================================================
     */
    storage_close_database(
        db,
        insert_statement
    );

    return NULL;
}