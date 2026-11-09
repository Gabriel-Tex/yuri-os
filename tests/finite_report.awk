# O Analista imprime os PIDs antes do relatório do serviço.
/^[1-9][0-9]*$/ { if (report || seen[$0]++) invalid=1; pids++; next }
/^criados: [0-9]+$/ { if (report++) invalid=1; created=$2; next }
/^concluídos: [0-9]+$/ { completed=$2; next }
/^satisfeitos: [0-9]+$/ { satisfied=$2; next }
/^insatisfeitos: [0-9]+$/ { unsatisfied=$2; next }
/^taxa de satisfação: [0-9]+\/[0-9]+ / { rate=$4; next }
/^tempo total: [0-9]+\.[0-9]+ ms$/ { elapsed=$3; next }
{ invalid=1 }
END {
    if (invalid || report != 1 || pids != expected || created != expected ||
        completed != expected || satisfied == "" || unsatisfied == "" ||
        satisfied + unsatisfied != expected || !rate || elapsed <= 0) exit 1;
    split(rate, fraction, "/");
    if (fraction[1] != satisfied || fraction[2] != expected) exit 1;
    if (emit) printf "%s,%s,%s\n", satisfied, completed, elapsed;
}
