.PHONY: all escalonador memoria entrada-saida executar-escalonador executar-memoria executar-es clean

all: escalonador memoria entrada-saida

escalonador:
	$(MAKE) -C escalonador

memoria:
	$(MAKE) -C memoria

entrada-saida:
	$(MAKE) -C entrada_saida

executar-escalonador: escalonador
	./escalonador/escalonador \
		escalonador/entradas/entrada_alternancia.txt

executar-memoria: memoria
	./memoria/escalonador \
		memoria/entradas/entradaMemoria.txt

executar-es: entrada-saida
	./entrada_saida/simulador \
		entrada_saida/entradas/entrada_ES.txt

clean:
	$(MAKE) -C escalonador clean
	$(MAKE) -C memoria clean
	$(MAKE) -C entrada_saida clean
