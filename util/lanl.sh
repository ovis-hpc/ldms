#!/bin/bash
RABBITADMIN=rabbitmqadmin
# Bash command to return the broker to a virgin state and create this configuration:
rabbitmqctl stop_app && \
rabbitmqctl reset && \
rabbitmqctl start_app && \
rabbitmqctl add_vhost user && \
rabbitmqctl add_vhost priv && \
rabbitmqctl add_vhost test && \
rabbitmqctl add_user peter mcgregor && \
rabbitmqctl add_user rabbituser mcgregor && \
rabbitmqctl add_user tester mcgregor && \
rabbitmqctl set_permissions -p / peter '.*' '.*' '.*' && \
rabbitmqctl set_permissions -p user peter '.*' '.*' '.*' && \
rabbitmqctl set_permissions -p priv peter '.*' '.*' '.*' && \
rabbitmqctl set_permissions -p test peter '.*' '.*' '.*' && \
rabbitmqctl set_permissions -p user rabbituser '.*' '.*' '.*' && \
rabbitmqctl set_permissions -p priv rabbituser '.*' '.*' '.*' && \
rabbitmqctl set_permissions -p test tester '.*' '.*' '.*' && \
rabbitmqctl set_user_tags peter administrator monitoring

#rabbitmqctl delete_user guest

set -x
# commands to create queues and bindings:
for vhost in user priv; do 
  #  for pair in syslog-bz.80 syslog-cj.80 syslog-ct.80 syslog-gl.6000 syslog-hb.80 syslog-ls.80 syslog-mp.400 syslog-ml.400 syslog-mu.400 syslog-pi.400 syslog-tfta.80 syslog-wf.400 syslog-service.400 syslog-l1.80 syslog-l2.80 syslog-l3.80 syslog-cg.80 syslog-cl.80 syslog-gpfs.80 syslog-tdms.80 syslog-infra.80 syslog-yellow.1000 moab-ct.80 moab-mu.80 moab-tt.80; do 
  for pair in syslog-bz.80 ldms-mutrino.80; do
    arrPair=(${pair//./ }); 
    name=${arrPair[0]}; 
    capacity=$(( ${arrPair[1]}*1024*1024 )); 
    echo In vhost $vhost creating queue $name; 
# delete it just in case it's already there, silently.
    $RABBITADMIN -u peter -p mcgregor -V $vhost delete queue name=$name-q >/dev/null 2>&1
    $RABBITADMIN -u peter -p mcgregor -V $vhost declare queue name=$name-q durable=true 'arguments={"x-max-length-bytes":'${capacity}'}'; 
    nameArr=(${name//-/ }); 
    case ${nameArr[0]} in 
	syslog) key=${nameArr[1]}.#
		;; 
	moab) key=${nameArr[1]}.${nameArr[1]}-drm.moab
		;; 
    esac; 
    echo Binding to amq.topic with routing key $key; 
    $RABBITADMIN -u peter -p mcgregor -V $vhost declare binding source=amq.topic destination=$name-q routing_key=$key;

  done; 
done

# ./amqp_listen localhost 5672 amq.topic '#'
# ./amqp_sendstring localhost 5672 amq.topic 'ldms.container.metric.id' "234.4"

