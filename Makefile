generate-ninja:
	@cmake -H. -Bbuild -GNinja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON

deps:
	@sudo docker run --network="host" -it appcelerator/etcd sh -c 'etcd -listen-client-urls http://0.0.0.0:2379 -advertise-client-urls http://etcd-srv:2379'
