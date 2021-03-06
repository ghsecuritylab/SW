#include "client_thread.h"
#include "request.h"
#include "lwip/opt.h"
#include "lwip/arch.h"
#include "lwip/api.h"

#include "memory_access.h"
#include "ftp_server.h"


int8_t send_data(void *data, size_t data_size, struct netconn *conn) {
    if(netconn_write(conn, data, data_size, NETCONN_COPY) != ERR_OK) {
    	xprintf("netconn_write error\r\n");
    	return 0;
    }
    return 1;
}


int8_t send_msg(char *status, char *msg, struct netconn *conn) {	
    char respond[MAX_RESPOND_LEN]; 

    sprintf(respond, "%s %s\n", status, msg); 
    xprintf("RESPOND: %s", respond);
    send_data(respond, sizeof(char)*strlen(respond), conn);
    return 1;    
}


uint8_t recv_request(struct netconn *conn, Request *request) {

    ((conn)->recv_timeout = (10000));
    struct netbuf *buf; 
    if(netconn_recv(conn, &buf) != ERR_OK) {
        if(conn->last_err == ERR_TIMEOUT) {
            xprintf("netconn_recv timeout error\r\n");
        } else {
            xprintf("netconn_recv error\r\n");
        }
        return 0;
    }  

    char *msg;
    u16_t len;

    if(netbuf_data(buf, (void *)&msg, &len) != ERR_OK) {
        xprintf("netbuf_data error\r\n");
        return 0;
    }	

    /*if(netbuf_next(buf) != -1)
        xprintf("There is more buffer parts\r\n");*/

    //xprintf("REQUEST: %s\n", msg);
    //xprintf(msg);
    update_request(request, msg);

    netbuf_delete(buf);
    return 1;
}


void type_response(char *args, ClientData *client_data) {
    if(strcmp(args, "I") == 0)
        send_msg("200", "Binary mode accepted", client_data->conn);
    else if(strcmp(args, "A") == 0)
        send_msg("200", "ASCII mode accepted", client_data->conn);
    else
        send_msg("502", "Mode not implemented", client_data->conn);
}

void pasv_response(ClientData *client_data) {
    char respond[MAX_RESPOND_LEN]; 
    sprintf(respond, "Entering Passive Mode (%u,%u,%u,%u,%u,%u)",
            ip4_addr1(&gnetif.ip_addr),
			ip4_addr2(&gnetif.ip_addr),
			ip4_addr3(&gnetif.ip_addr),
			ip4_addr4(&gnetif.ip_addr),
            DATA_PORT/256,
            DATA_PORT % 256);  

    send_msg("227", respond, client_data->conn);
}

void list_response(ClientData *client_data) {

    send_msg("150", "Listing", client_data->conn);
    struct netconn *client_data_conn;
	if(netconn_accept(data_conn, &client_data_conn) != ERR_OK) {
        xprintf("netconn_accept error\n");
        xprintf("list\n");
        return;
    } 

    char list_data[MAX_LIST_DATA_LEN];
    xprintf(client_data->current_dir);
    list_directory(client_data->current_dir, list_data, MAX_LIST_DATA_LEN);
    xprintf("LIST DATA: %s\r\n", list_data);

    send_data(list_data, sizeof(char)*strlen(list_data), client_data_conn);
    send_msg("226", "List OK", client_data->conn);

    netconn_close(client_data_conn);
    netconn_delete(client_data_conn);
}

void cwd_response(char *args, ClientData *client_data) {
    if(!change_directory(client_data->current_dir, args)) {
        xprintf("cwd error\r\n");
        send_msg("550", "File unavailable.", client_data->conn);
        return;
    }
    send_msg("250", "OK.", client_data->conn);
}

int8_t send_file(char *current_path, char *filename, struct netconn *conn) {
    FIL file;
    if(!open_file(current_path, filename, &file)) {
        xprintf("open_file error\r\n");
        return 0;
    }

    char buf[BUFFER_LEN];
    unsigned int br;
    while(1) {
		if(f_read(&file, buf, sizeof(char)*BUFFER_LEN, &br) != FR_OK) {
			xprintf("f_read error\r\n");
			close_file(&file);	
            return 0;		
		}		
        send_data(buf, br, conn);

        if(br < sizeof(char)*BUFFER_LEN) 
        	break;
    }

	close_file(&file);	
    return 1;
}


int8_t recv_file(char *current_path, char *filename, struct netconn *conn) {
    FIL file;
    if(!create_file(current_path, filename, &file)) {
        xprintf("create_file error\r\n");
        return 0;
    }  

    struct netbuf *buf;
    void *data;
    uint16_t data_size;
    unsigned int bw;

    conn->recv_timeout = 10000;
    while(netconn_recv(conn, &buf) == ERR_OK) {
        do {
            netbuf_data(buf, &data, &data_size);
            if(f_write(&file, data, data_size, &bw) != FR_OK) {
            	xprintf("f_write error\r\n");
            }
        } while (netbuf_next(buf) >= 0);
        netbuf_delete(buf);
    }
    close_file(&file);
    return 1;
}

void retr_response(char *args, ClientData *client_data) {
    send_msg("150", "File transfer", client_data->conn);

    struct netconn *client_data_conn;
	if(netconn_accept(data_conn, &client_data_conn) != ERR_OK) {
        xprintf("netconn_accept error\n");
        xprintf("retr\n");
        return;
    } 

    send_file(client_data->current_dir, args, client_data_conn);

    send_msg("226", "Transfer completed", client_data->conn);
 
    netconn_close(client_data_conn);
    netconn_delete(client_data_conn);
}

void stor_response(char *args, ClientData *client_data) {
    send_msg("150", "File transfer", client_data->conn);

    struct netconn *client_data_conn;
	if(netconn_accept(data_conn, &client_data_conn) != ERR_OK) {
        xprintf("netconn_accept error\n");
        xprintf("stor\n");
        return;
    } 

    recv_file(client_data->current_dir, args, client_data_conn);

    send_msg("226", "Transfer completed", client_data->conn);

 	netconn_close(client_data_conn);
    netconn_delete(client_data_conn);
}

void mkd_response(char *args, ClientData *client_data) {
    if(!create_dir(client_data->current_dir, args)) {
        xprintf("create_dir error\r\n");
        return;
    }
    send_msg("257", "Directory created", client_data->conn);
}


void quit_response(ClientData *client_data) {
    send_msg("221", "Quit", client_data->conn);
}


void serve_request(Request *req, ClientData *client_data) {
    struct netconn *client_conn = client_data->conn;
    char *command = req->command;

    if (strcmp(command, "USER") == 0) {
        send_msg("230", "Success", client_conn);
    } else if (strcmp(command, "SYST") == 0) {
        send_msg("215", "EMBOS", client_conn);
    } else if (strcmp(command, "PWD") == 0) {
            send_msg("257", client_data->current_dir, client_conn);
    } else if ((strcmp(command, "TYPE") == 0)) {
        type_response(req->args, client_data);
    } else if (strcmp(command, "PASV") == 0) {
        pasv_response(client_data);
    } else if (strcmp(command, "LIST") == 0) {
        list_response(client_data);
    } else if (strcmp(command, "CWD") == 0) {
        cwd_response(req->args, client_data);
    } else if (strcmp(command, "RETR") == 0) {
        retr_response(req->args, client_data);
    } else if (strcmp(command, "STOR") == 0) {
        stor_response(req->args, client_data);
    } else if (strcmp(command, "MKD") == 0) {
        mkd_response(req->args, client_data);
    } else if(strcmp(command, "QUIT") == 0) {
        quit_response(client_data);
    } else {
        send_msg("502", "Command not implemented :(", client_conn);
    }
}

void serve_client(void *client_conn) {
    ClientData client_data;
    client_data.conn = (struct netconn *)client_conn;
    strcpy(client_data.current_dir, "/");

    send_msg("220",  "Welcome", client_data.conn);

    Request req;
    while(1) {
        xprintf("wait for request\r\n");
        if(!recv_request(client_data.conn, &req)) {
            xprintf("Request error\r\n");     
            netconn_close(client_data.conn);
            netconn_delete(client_data.conn);    
            xprintf("closing client connection\r\n");     
            return;      
        }

        xprintf("Start serving reqest, COMMAND: %s, ARGS: %s\r\n", req.command, req.args);
        serve_request(&req, &client_data);
    }    
}

void serve_client_task(void *arg) {
    while(1) {
        ClientData client_data;
        
        if(!xQueueReceive(clients_queue, &client_data.conn, portMAX_DELAY)) {
            xprintf("xQueueReceive error, serve_client_task\r\n");
        }
        strcpy(client_data.current_dir, "/");

        send_msg("220",  "Welcome", client_data.conn);

        Request req;
        while(1) {
            if(!recv_request(client_data.conn, &req)) {
                xprintf("Request error\r\n");
                netconn_close(client_data.conn);
                netconn_delete(client_data.conn); 
                xprintf("closing client connection\r\n");   
                //BaseType_t xReturned = xTaskCreate(serve_client_task, "task_name", 512, NULL, 16, NULL);

                //if( xReturned != pdPASS )
                   // xprintf("xTaskCreate error, client: %d\n", -1); 
                break;      
            }
            xprintf("Start serving reqest, COMMAND: %s, ARGS: %s\r\n", req.command, req.args);
            serve_request(&req, &client_data);            
            vTaskDelay(100);
        }    
    }

}
