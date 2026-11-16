# Sensor-Gateway Embedded-Linux

1. Giới thiệu

Sensor Gateway là một hệ thống thu thập và xử lý dữ liệu từ các sensor node trên nền tảng Linux.

Gateway được xây dựng bằng ngôn ngữ C và sử dụng các cơ chế của Linux/POSIX để xử lý kết nối mạng, đa luồng, đồng bộ dữ liệu, lưu trữ và ghi log.

Dự án được thực hiện trong phạm vi Step 0 đến Step 7 của bài tập Embedded Linux.

2. Chức năng chính

Kết nối và nhận dữ liệu từ nhiều sensor node.

Xử lý dữ liệu cảm biến.

Quản lý dữ liệu giữa các thread.

Lưu trữ dữ liệu bằng SQLite.

Ghi log hoạt động của hệ thống.

Hỗ trợ xử lý lỗi và shutdown có kiểm soát.

Kiểm tra và đánh giá hoạt động của chương trình.

3. Kiến trúc hệ thống

Hệ thống gồm các thành phần chính:

Connection Manager: quản lý kết nối và nhận dữ liệu từ sensor.

Data Manager: xử lý và kiểm tra dữ liệu.

Storage Manager: lưu dữ liệu vào cơ sở dữ liệu.

Logger: ghi lại các sự kiện của hệ thống.

Shared Buffer: trung gian trao đổi dữ liệu giữa các thành phần.

Các thành phần được tổ chức theo mô hình đa luồng và sử dụng các cơ chế đồng bộ của POSIX.

4. Công nghệ sử dụng

C

Linux / Embedded Linux

POSIX Threads

TCP/IP Socket

SQLite

FIFO

GCC / Make

Valgrind

5. Biên dịch

make clean
make

6. Chạy chương trình

./server <tcp_port> <http_port>

Ví dụ:

./server 9000 8080

7. Kiểm thử

Dự án có các chương trình và script kiểm thử trong thư mục test/.

Có thể sử dụng Valgrind để kiểm tra lỗi bộ nhớ và tài nguyên:

valgrind --leak-check=full --show-leak-kinds=all --track-fds=yes ./server 9000 8080

8. Cấu trúc dự án

Sensor-Gateway/
├── include/       # Header files
├── src/           # Source code
├── test/          # Test files
├── evidence/      # Test evidence
├── Makefile
└── README.md

9. Phạm vi dự án

Phiên bản hiện tại tập trung vào các chức năng được yêu cầu từ Step 0 đến Step 7, bao gồm:

Giao tiếp với sensor;

Xử lý dữ liệu;

Quản lý bộ nhớ và đồng bộ;

Lưu trữ dữ liệu;

Logging;

Xử lý lỗi;

Shutdown hệ thống.

Các chức năng bổ sung ngoài phạm vi Step 0–7 không được trình bày trong README này.
