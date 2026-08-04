total = 0
for i in range(1, 5_000_001):
    if i % 2 == 0:
        continue
    total += i
print(total)
