/* ---- Hằng số và kiểu liệt kê dùng chung ---- */
#define RX_BUF_SIZE 256
#define MAX_CONN 64
#define MAX_SENSORS 64
typedef enum { STATE_COLLECTING, STATE_NORMAL, STATE_ALERT } sensor_state_t;
typedef enum { SQL_CONNECTED, SQL_LOST, SQL_DOWN } sql_state_t;

/* ---- Sensor ---- */
/* sensor_data_t: xem Mục 3.2 */
typedef struct { /* một dòng của file room-sensor */
    uint16_t room;
    sensor_id_t id;
    sensor_type_t type; 
} sensor_info_t;    

/* ---- sbuffer ---- */ 
typedef struct sbuffer_node {
    sensor_data_t data;
    bool read_by_data; /* Data manager đã đọc */
    bool read_by_storage; /* Storage manager đã đọc */
    struct sbuffer_node *next;
} sbuffer_node_t;

typedef struct {    
    sbuffer_node_t *head, *tail;
    size_t size;
    bool shutdown; /* báo kết thúc */
    pthread_mutex_t lock;
    pthread_cond_t not_empty; /* đánh thức thread đọc */
} sbuffer_t;

/* ---- Connection manager ---- */
typedef struct {
    int fd;
    const protocol_adapter_t *adapter;
    uint8_t rx[RX_BUF_SIZE]; /* bộ đệm nhận riêng */
    size_t rx_len;
    bool identified; /* đã biết sensor ID */
    sensor_id_t sensor_id;
} connection_t;

typedef struct {
    int listen_fd[4]; /* cổng P .. P+3 */
    connection_t conns[MAX_CONN];
    size_t n_conns;
} connmgr_t;

/* ---- Data manager ---- */
typedef struct {
    sensor_info_t info;
    double window[RUN_AVG_LENGTH]; /* vòng đệm giá trị trung binh */
    size_t count, next;
    double running_avg;
    sensor_state_t state;
} datamgr_sensor_t;

typedef struct { datamgr_sensor_t sensors[MAX_SENSORS]; size_t n; } datamgr_t;
typedef void (*sensor_handler_t)(datamgr_t *, datamgr_sensor_t *, const sensor_data_t *);

/* ---- Storage manager + SQLite ---- */
typedef struct {
    sqlite3 *db;
    sqlite3_stmt *insert;
    sql_state_t state;
    int attempts;
    const char *db_path;
} storagemgr_t;

/* SQLite: bảng SensorData(id, type, value, ts), xem Mục 3.6 */
/* ---- Shared status (shared memory) ---- */
typedef struct {
    sensor_id_t id; uint16_t room; sensor_type_t type;
    double last_value, running_avg; bool has_avg;
    sensor_state_t state; bool connected; time_t last_update;
} sensor_status_t;

typedef struct {
    pthread_mutex_t lock; /* PROCESS_SHARED + ROBUST */
    uint64_t received, processed, stored;
    uint32_t sbuffer_size;
    sql_state_t sql_state; uint8_t sql_retries;
    time_t start_time;
    uint16_t sensor_count;
    sensor_status_t sensors[MAX_SENSORS];
} gateway_status_t;

/* ---- Ngữ cảnh gốc của main process ---- */
typedef struct {
    uint16_t base_port, http_port;
    sbuffer_t sbuf;
    connmgr_t conn;
    datamgr_t data;
    storagemgr_t stor;
    gateway_status_t *status; /* NULL cho tới Step 8 */
    pid_t log_pid, web_pid;
    volatile sig_atomic_t stop;
} gateway_t;