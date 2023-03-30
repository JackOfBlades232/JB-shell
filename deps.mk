string_set.o: src/utils/string_set.c src/utils/string_set.h
int_set.o: src/utils/int_set.c src/utils/int_set.h
interpreter.o: src/interpreter/interpreter.c \
 src/interpreter/interpreter.h src/interpreter/../tokeniz/word_list.h \
 src/interpreter/../tokeniz/word.h \
 src/interpreter/../execution/parse_command.h \
 src/interpreter/../execution/../tokeniz/word_list.h \
 src/interpreter/../execution/command.h \
 src/interpreter/../execution/cmd_res.h \
 src/interpreter/../execution/execute_command.h \
 src/interpreter/../execution/cmd_res.h
parse_command.o: src/execution/parse_command.c \
 src/execution/parse_command.h src/execution/../tokeniz/word_list.h \
 src/execution/../tokeniz/word.h src/execution/command.h \
 src/execution/cmd_res.h
execute_command.o: src/execution/execute_command.c \
 src/execution/execute_command.h src/execution/../tokeniz/word_list.h \
 src/execution/../tokeniz/word.h src/execution/command.h \
 src/execution/cmd_res.h src/execution/parse_command.h \
 src/execution/../utils/int_set.h
command.o: src/execution/command.c src/execution/command.h
line_tokenization.o: src/tokeniz/line_tokenization.c \
 src/tokeniz/line_tokenization.h src/tokeniz/word_list.h \
 src/tokeniz/word.h src/tokeniz/../edit/input.h
word_list.o: src/tokeniz/word_list.c src/tokeniz/word_list.h \
 src/tokeniz/word.h
word.o: src/tokeniz/word.c src/tokeniz/word.h
autocomplete.o: src/edit/autocomplete.c src/edit/autocomplete.h \
 src/edit/input.h src/edit/lookup.h src/edit/../utils/string_set.h
lookup.o: src/edit/lookup.c src/edit/lookup.h \
 src/edit/../utils/string_set.h
input.o: src/edit/input.c src/edit/input.h src/edit/autocomplete.h
