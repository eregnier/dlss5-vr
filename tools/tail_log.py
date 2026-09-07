import sys, time

path = sys.argv[1]
duration = int(sys.argv[2]) if len(sys.argv) > 2 else 10
end_time = time.time() + duration

last_size = 0
while time.time() < end_time:
    try:
        with open(path, 'r', encoding='utf-8', errors='ignore') as f:
            f.seek(last_size)
            new_data = f.read()
            if new_data:
                sys.stdout.write(new_data)
                sys.stdout.flush()
            last_size = f.tell()
    except Exception:
        pass
    time.sleep(1)
