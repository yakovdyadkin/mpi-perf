import logging
import argparse
import sys
import os
from azure.kusto.data import KustoConnectionStringBuilder, DataFormat
from azure.kusto.ingest import QueuedIngestClient, IngestionProperties, FileDescriptor

class KustoIngest:

    CONFIG_FILE_NAME = "config-kusto.json"
    CONFIG_BLOB_PATH = "config-kusto.json"
    STORAGE_CONTAINER_NAME = "customscript"
        
    def __init__(self, argv):
        self.logger = logging.getLogger("KustoIngest")
        logging.basicConfig(level=logging.INFO)
        self.parse_args(argv)
    
    def parse_args(self, argv):
        parser = argparse.ArgumentParser(description= "Iperf testing args")
        parser.add_argument("-f", "--flows", dest="flows", type=int, help="Number of flows.")
        self.args = parser.parse_args(argv[1:])
        
    def ingest_to_kusto(self, kusto_dir):
        ingestion_props = IngestionProperties(database="WarpPPE", table="PerfLogsMPI", data_format=DataFormat.CSV)
        ingest_cluster_url = "https://ingest-azwan.kusto.windows.net"
        kcsb = KustoConnectionStringBuilder.with_aad_managed_service_identity_authentication(ingest_cluster_url)
        ingest_client = QueuedIngestClient(kcsb)
        if not os.path.exists(kusto_dir):
            print("Kusto directory doesn't exist : ", kusto_dir)
            return
        output_files = [os.path.join(kusto_dir, file) for file in os.listdir(kusto_dir) if (os.path.isfile(os.path.join(kusto_dir, file)) and file.lower().startswith("tcp"))]
        print("output files unsorted : ", output_files)
        output_files.sort(key=os.path.getmtime)
        print("output files sorted : ", output_files)


        # since we are sorting by time, we should ignore the latest one as that would still be written to.
        n = self.args.flows
        for f in output_files[:-n]:
            ingest_client.ingest_from_file(f, ingestion_properties=ingestion_props)
            if os.path.exists(f):
                print("removing file : ", f)
                os.remove(f)

    def run(self):
        self.ingest_to_kusto("/mnt/tcp-logs")

if __name__ == '__main__':
    netperf = KustoIngest(sys.argv)
    netperf.run()

