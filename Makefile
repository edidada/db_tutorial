SRC = $(wildcard src/*.c)

db: $(SRC)
	gcc $(SRC) -o db -Isrc

run: db
	./db mydb.db

clean:
	rm -f db *.db

test: db
	bundle exec rspec

format: $(SRC)
	clang-format -style=Google -i $(SRC)
