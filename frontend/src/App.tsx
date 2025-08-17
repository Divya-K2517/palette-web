import React, { useState, useEffect } from 'react';
import { Search, Activity, Image, BarChart3, RefreshCw, AlertTriangle } from 'lucide-react';

const API_BASE = 'http://localhost:8080';
//two backend endpoints are /graphql and /health
const VectorSearchFrontend = () => {
  interface SearchResultsType {
    data: {
      search_concepts: {
        mission_id: string;
        query: string;
        nodes: Array<{
          id: string;
          name: string;
          similarityScore: number;
          timestamp: string;
          healthStatus: number;
          level: number;
          embedding: number[];
        }>;
        processing_time_ms: number;
        system_status: any;
        pinterest_integration_status: string;
        timestamp: number;
      };
    };
  }
  interface SystemHealthType {
    // nlohmann::json data = {
    //                 {"system_health", {
    //                     {"status", healthMetrics.toJson()},
    //                     {"timestamp", CoreSystems::utils::getTimestampMs()},
    //                     //{"uptime_ms", CoreSystems::utils::getTimestampMs()},
    //                     {"version", "1.0.0"}
    //                     {"primary_engine_operational", systemManager->getPrimaryVectorEngine() ? systemManager->getPrimaryVectorEngine()->isEngineOperational() : false},
    //                     {"backup_engine_operational", systemManager->getBackupVectorEngine() ? systemManager->getBackupVectorEngine()->isEngineOperational() : false}
    //                 }}
    //             };
    data: {
      system_health: {
        status: number;
        cpu_useage: number;
        memory_usage: number;
        active_connections: number;
        error_rate: number;
        
        timestamp: number;
        version: string;
        primary_engine_operational: boolean;
        backup_engine_operational: boolean;
      };
    };
  }
  interface TelemetryReportType {
    // nlohmann::json data = {
    //                     {"total_queries", report.total_queries},
    //                     {"average_response_time", report.average_response_time},
    //                     {"error_rate", report.error_rate},
    //                     {"telemetry_records", report.telemetry_records},
    //                     {"timestamp", report.timestamp}
    //                 };
    data: {
      total_queries: number;
      average_response_time: number;
      error_rate: number;
      telemetry_records: number;
      timestamp: number;
    };
  }
  const [searchQuery, setSearchQuery] = useState('');
  const [searchResults, setSearchResults] = useState<SearchResultsType | null>(null);
  const [systemHealth, setSystemHealth] = useState<SystemHealthType | null>(null);
  const [telemetryReport, setTelemetryReport] = useState<TelemetryReportType | null>(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState('');

  //graphql request helper
  const makeGraphQLRequest = async (query: string, variables = {}) => {
    try {
      const response = await fetch(`${API_BASE}/graphql`, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
        },
        body: JSON.stringify({
          query,
          variables
        })
      });
      
      const data = await response.json();
      if (data.errors) {
        throw new Error(data.errors[0].message);
      }
      return data.data;
    } catch (err: any) {
      throw new Error(`GraphQL Error: ${err.message}`);
    }
  };

  //search for nodes
  const handleSearch = async () => {
    if (!searchQuery.trim()) return;
    
    setLoading(true);
    setError('');
    
    try {
      const query = `
        query SearchConcepts($query: String!, $limit: Int) {
          search_concepts(query: $query, limit: $limit) {
            mission_id
            query
            nodes {
              id
              name
              similarityScore
              level
              timestamp
              healthStatus
            }
            processing_time_ms
            system_status {
              cpuUsage
              memoryUsage
              activeConnections
              errorRate
              healthStatus
            }
            pinterest_integration_status
            timestamp
          }
        }
      `;
      
      const data = await makeGraphQLRequest(query, {
        //gets related nodes based on search query
        query: searchQuery,
        limit: 10
      });
      
      setSearchResults(data.search_concepts); 
    } catch (err: any) {
      setError(err.message);
    } finally {
      setLoading(false);
    }
  };

  //get system health
  const fetchSystemHealth = async () => {
    try {
      const query = `
        query SystemHealth {
          system_health {
            status {
              cpuUsage
              memoryUsage
              activeConnections
              errorRate
              healthStatus
            }
            timestamp
            version
            primary_engine_operational
            backup_engine_operational
          }
        }
      `;
      
      const data = await makeGraphQLRequest(query);
      setSystemHealth(data.system_health);
    } catch (err: any) {
      setError(err.message);
    }
  };

  //get telemetry report
  const fetchTelemetryReport = async () => {
    try {
      const query = `
        query TelemetryReport {
          telemetry_report {
            total_queries
            average_response_time
            error_rate
            telemetry_records
            timestamp
          }
        }
      `;
      
      const data = await makeGraphQLRequest(query);
      setTelemetryReport(data.telemetry_report);
    } catch (err: any) {
      setError(err.message);
    }
  };

  //emergency restart
  const handleEmergencyRestart = async (subsystem: string) => {
    try {
      const mutation = `
        mutation EmergencyRestart($subsystem: String!) {
          emergency_restart(subsystem: $subsystem) {
            subsystem
            success
            message
            timestamp
          }
        }
      `;
      
      const data = await makeGraphQLRequest(mutation, { subsystem });
      alert(`Restart ${subsystem}: ${data.emergency_restart.message}`);
      
      // Refresh system health after restart
      setTimeout(fetchSystemHealth, 1000);
    } catch (err: any) {
      setError(err.message);
    }
  };

  //clear cache
  const handleClearCache = async () => {
    try {
      const mutation = `
        mutation ClearCache {
          clear_cache {
            success
            message
            timestamp
          }
        }
      `;
      
      const data = await makeGraphQLRequest(mutation);
      alert(`Clear Cache: ${data.clear_cache.message}`);
    } catch (err: any) {
      setError(err.message);
    }
  };

  //load initial data on system health and telemetry
  useEffect(() => {
    fetchSystemHealth();
    fetchTelemetryReport();
  }, []);

  const getHealthStatusColor = (status: number) => {
    switch (status) {
      case 0: return 'text-green-600'; // NOMINAL
      case 1: return 'text-yellow-600'; // DEGRADED  
      case 2: return 'text-red-600'; // CRITICAL
      default: return 'text-gray-600';
    }
  };

  const getHealthStatusText = (status: number) => {
    switch (status) {
      case 0: return 'NOMINAL';
      case 1: return 'DEGRADED';
      case 2: return 'CRITICAL';
      default: return 'UNKNOWN';
    }
  };

  return (
    <div className="min-h-screen bg-gray-100 p-4">
      <div className="max-w-6xl mx-auto">
        {/* Header */}
        <div className="bg-white rounded-lg shadow-md p-6 mb-6">
          <h1 className="text-3xl font-bold text-gray-800 mb-2">
            🚀 Vector Search System - Ground Control
          </h1>
          <p className="text-gray-600">Test interface for your C++ backend</p>
        </div>

        {/* Error Display */}
        {error && (
          <div className="bg-red-100 border border-red-400 text-red-700 px-4 py-3 rounded mb-6">
            <div className="flex items-center">
              <AlertTriangle className="w-5 h-5 mr-2" />
              <span>{error}</span>
            </div>
          </div>
        )}

        <div className="grid grid-cols-1 lg:grid-cols-3 gap-6">
          {/* Search Section */}
          <div className="lg:col-span-2 bg-white rounded-lg shadow-md p-6">
            <h2 className="text-xl font-semibold mb-4 flex items-center">
              <Search className="w-5 h-5 mr-2" />
              Vector Search
            </h2>
            
            <div className="flex gap-2 mb-4">
              <input
                type="text"
                value={searchQuery}
                onChange={(e) => setSearchQuery(e.target.value)}
                onKeyPress={(e) => e.key === 'Enter' && handleSearch()}
                placeholder="Enter search query..."
                className="flex-1 p-3 border border-gray-300 rounded-lg focus:outline-none focus:ring-2 focus:ring-blue-500"
              />
              <button
                onClick={handleSearch}
                disabled={loading || !searchQuery.trim()}
                className="px-6 py-3 bg-blue-600 text-white rounded-lg hover:bg-blue-700 disabled:opacity-50 disabled:cursor-not-allowed"
              >
                {loading ? '🔄' : 'Search'}
              </button>
            </div>

            {/* Search Results */}
            {searchResults && (
              <div className="mt-6">
                <div className="bg-gray-50 p-4 rounded-lg mb-4">
                  <div className="grid grid-cols-2 md:grid-cols-4 gap-4 text-sm">
                    <div>
                      <span className="font-medium">Mission ID:</span>
                      <div className="font-mono text-xs">{searchResults.data.search_concepts.mission_id}</div>
                    </div>
                    <div>
                      <span className="font-medium">Processing Time:</span>
                      <div>{searchResults.data.search_concepts.processing_time_ms}ms</div>
                    </div>
                    <div>
                      <span className="font-medium">Nodes Found:</span>
                      <div>{searchResults.data.search_concepts.nodes.length}</div>
                    </div>
                    <div>
                      <span className="font-medium">Pinterest Status:</span>
                      <div className="text-green-600">{searchResults.data.search_concepts.pinterest_integration_status}</div>
                    </div>
                  </div>
                </div>

                <div className="space-y-3">
                  {searchResults.data.search_concepts.nodes.map((node, index) => (
                    <div key={node.id} className="border border-gray-200 rounded-lg p-4">
                      <div className="flex justify-between items-start mb-2">
                        <h3 className="font-medium text-lg">{node.name}</h3>
                        <div className="flex gap-2 text-sm">
                          <span className="bg-blue-100 text-blue-800 px-2 py-1 rounded">
                            Level {node.level}
                          </span>
                          <span className={`px-2 py-1 rounded ${getHealthStatusColor(node.healthStatus)} bg-gray-100`}>
                            {getHealthStatusText(node.healthStatus)}
                          </span>
                        </div>
                      </div>
                      <div className="text-sm text-gray-600 grid grid-cols-2 gap-4">
                        <div>Similarity Score: <span className="font-mono">{node.similarityScore.toFixed(4)}</span></div>
                        <div>ID: <span className="font-mono text-xs">{node.id}</span></div>
                      </div>
                    </div>
                  ))}
                </div>
              </div>
            )}
          </div>

          {/* System Panel */}
          <div className="space-y-6">
            {/* System Health */}
            <div className="bg-white rounded-lg shadow-md p-6">
              <div className="flex justify-between items-center mb-4">
                <h2 className="text-xl font-semibold flex items-center">
                  <Activity className="w-5 h-5 mr-2" />
                  System Health
                </h2>
                <button
                  onClick={fetchSystemHealth}
                  className="p-2 text-gray-600 hover:text-gray-800"
                  title="Refresh"
                >
                  <RefreshCw className="w-4 h-4" />
                </button>
              </div>

              {systemHealth && (
                <div className="space-y-3">
                  <div className="bg-gray-50 p-3 rounded">
                    <div className="text-sm font-medium mb-2">Overall Status</div>
                    <div className={`font-bold ${getHealthStatusColor(systemHealth.data.system_health.status)}`}>
                      {getHealthStatusText(systemHealth.data.system_health.status)}
                    </div>
                  </div>

                  <div className="grid grid-cols-2 gap-3 text-sm">
                    <div>
                      <div className="font-medium">CPU Usage</div>
                      <div>{systemHealth.data.system_health.cpu_useage.toFixed(1)}%</div>
                    </div>
                    <div>
                      <div className="font-medium">Memory Usage</div>
                      <div>{systemHealth.data.system_health.memory_usage.toFixed(1)} MB</div>
                    </div>
                    <div>
                      <div className="font-medium">Active Connections</div>
                      <div>{systemHealth.data.system_health.active_connections}</div>
                    </div>
                    <div>
                      <div className="font-medium">Error Rate</div>
                      <div>{(systemHealth.data.system_health.error_rate * 100).toFixed(2)}%</div>
                    </div>
                  </div>

                  <div className="pt-3 border-t">
                    <div className="text-sm space-y-1">
                      <div>Primary Engine: <span className={systemHealth.data.system_health.primary_engine_operational ? 'text-green-600' : 'text-red-600'}>
                        {systemHealth.data.system_health.primary_engine_operational ? '✓ Online' : '✗ Offline'}
                      </span></div>
                      <div>Backup Engine: <span className={systemHealth.data.system_health.backup_engine_operational ? 'text-green-600' : 'text-red-600'}>
                        {systemHealth.data.system_health.backup_engine_operational ? '✓ Online' : '✗ Offline'}
                      </span></div>
                    </div>
                  </div>
                </div>
              )}
            </div>

            {/* Telemetry */}
            <div className="bg-white rounded-lg shadow-md p-6">
              <div className="flex justify-between items-center mb-4">
                <h2 className="text-xl font-semibold flex items-center">
                  <BarChart3 className="w-5 h-5 mr-2" />
                  Telemetry
                </h2>
                <button
                  onClick={fetchTelemetryReport}
                  className="p-2 text-gray-600 hover:text-gray-800"
                  title="Refresh"
                >
                  <RefreshCw className="w-4 h-4" />
                </button>
              </div>

              {telemetryReport && (
                <div className="space-y-3 text-sm">
                  <div>
                    <div className="font-medium">Total Queries</div>
                    <div className="text-2xl font-bold text-blue-600">{telemetryReport.data.total_queries}</div>
                  </div>
                  <div>
                    <div className="font-medium">Avg Response Time</div>
                    <div>{telemetryReport.data.average_response_time.toFixed(2)}ms</div>
                  </div>
                  <div>
                    <div className="font-medium">Error Rate</div>
                    <div>{(telemetryReport.data.error_rate * 100).toFixed(2)}%</div>
                  </div>
                  <div>
                    <div className="font-medium">Records Stored</div>
                    <div>{telemetryReport.data.telemetry_records}</div>
                  </div>
                </div>
              )}
            </div>

            {/* Controls */}
            <div className="bg-white rounded-lg shadow-md p-6">
              <h2 className="text-xl font-semibold mb-4">System Controls</h2>
              <div className="space-y-3">
                <button
                  onClick={() => handleEmergencyRestart('primary')}
                  className="w-full p-2 bg-yellow-600 text-white rounded hover:bg-yellow-700 text-sm"
                >
                  Restart Primary Engine
                </button>
                <button
                  onClick={() => handleEmergencyRestart('backup')}
                  className="w-full p-2 bg-yellow-600 text-white rounded hover:bg-yellow-700 text-sm"
                >
                  Restart Backup Engine
                </button>
                <button
                  onClick={() => handleEmergencyRestart('telemetry')}
                  className="w-full p-2 bg-yellow-600 text-white rounded hover:bg-yellow-700 text-sm"
                >
                  Restart Telemetry
                </button>
                <button
                  onClick={handleClearCache}
                  className="w-full p-2 bg-red-600 text-white rounded hover:bg-red-700 text-sm"
                >
                  Clear Cache
                </button>
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
};

export default VectorSearchFrontend;