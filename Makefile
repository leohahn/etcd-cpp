deps:
	@sudo docker run --network="host" -p 2379:2379 -it appcelerator/etcd sh -c 'etcd -listen-client-urls http://0.0.0.0:2379 -advertise-client-urls http://etcd-srv:2379'
