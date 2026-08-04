values = []
for i in range(300_000):
    values.append(i % 1000)
print(f"{len(values)}:{sum(values)}")
