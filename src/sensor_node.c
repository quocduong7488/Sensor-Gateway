./sensor_node <loại> <id> <host> <cổng gốc P> <chu kỳ ms> [tùy chọn]
loại: temperature | humidity | motion | door | smoke_gas
tùy chọn: --fragment chia mỗi frame thành nhiều lần send(), có khoảng nghỉ
--glue gộp nhiều frame vào một lần send()
--corrupt thỉnh thoảng gửi frame sai (sai CRC, sai magic, sai độ dài)
./sensor_node temperature 10 127.0.0.1 1234 1000
./sensor_node smoke_gas 50 127.0.0.1 1234 2000 --corrupt