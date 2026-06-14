# symvers

Cole o arquivo `Module.symvers` gerado no build do seu kernel nesta pasta e renomeie-o para `Module.symvers`. 

Isso garantirá que o módulo da GPU seja compilado com as assinaturas de símbolos idênticas às do seu kernel ativo (`OP-WILD`), evitando o erro `Exec format error` no `insmod`.
