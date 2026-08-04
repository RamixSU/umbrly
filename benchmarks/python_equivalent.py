from time import perf_counter


t0 = perf_counter()
total = 0
for i in range(1, 300001):
    if i % 2 == 0:
        continue
    total += i
t1 = perf_counter()
print(f"loop={t1 - t0:.9f} result={total}")


def fib(n):
    if n < 2:
        return n
    return fib(n - 1) + fib(n - 2)


t2 = perf_counter()
fib_result = fib(27)
t3 = perf_counter()
print(f"fib={t3 - t2:.9f} result={fib_result}")

t4 = perf_counter()
message = ""
for k in range(200000):
    message = f"x {k} y"
t5 = perf_counter()
print(f"strings={t5 - t4:.9f} result={message}")
