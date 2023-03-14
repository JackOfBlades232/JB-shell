word.o: src/word.c src/word.h
word_list.o: src/word_list.c src/word_list.h src/word.h
line_tokenization.o: src/line_tokenization.c src/line_tokenization.h \
 src/word_list.h src/word.h
int_set.o: src/int_set.c src/int_set.h
command.o: src/command.c src/command.h
parse_command.o: src/parse_command.c src/parse_command.h src/word_list.h \
 src/word.h src/command.h
execute_command.o: src/execute_command.c src/execute_command.h \
 src/word_list.h src/word.h src/command.h src/cmd_res.h \
 src/parse_command.h src/int_set.h
prompt.o: src/prompt.c src/prompt.h src/line_tokenization.h \
 src/word_list.h src/word.h src/execute_command.h src/command.h \
 src/cmd_res.h
