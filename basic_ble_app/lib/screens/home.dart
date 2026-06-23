import 'dart:async'; //herramientas relacionadas con el tiempo

import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
// import 'package:permission_handler/permission_handler.dart';

class HomeScreen extends StatefulWidget {
  const HomeScreen({super.key});

  @override
  State<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen> {
  String estado = "Todavía no se escaneó";

  final Map<String, ScanResult> dispositivosEncontrados =
      {}; //crea un mapa (diccionario); la clave es el ID del dispositivo
  StreamSubscription<List<ScanResult>>? scanSubscription;
  //una variable que va guardar los scanResult
  //puede ser nulo (?) y son un dato "contínuo" (stream)

  BluetoothDevice? dispositivoConectado;
  //variable que almacena el dispositivo conectado
  //si no hay nada conectado vale null

  BluetoothCharacteristic? caracteristicaDatos;
  //Característica

  final Guid serviceUuid = Guid("12345678-1234-1234-1234-123456789abc");
  final Guid characteristicUuid = Guid("abcd1234-1234-1234-1234-abcdef123456");
  //UUID BLE (por eso "Guid") del servicio y de la característica

  // Future<bool> pedirPermisosBluetooth() async {
  //   //pide permiso
  //   final scan = await Permission.bluetoothScan.request();
  //   final connect = await Permission.bluetoothConnect.request();

  //   if (scan.isGranted && connect.isGranted) {
  //     return true;
  //   }

  //   estado = "Faltan permisos de Bluetooth";
  //   setState(() {});
  //   return false;
  // }

  Future<void> iniciarEscaneo() async {
    // final permisosOk = await pedirPermisosBluetooth();
    // if (!permisosOk) {
    //   return; //si no tiene el permiso no se ejecuta el resto
    // }

    //Future<void> = void; async permite usar await
    await FlutterBluePlus.stopScan(); //termina cualquier escaneo previo
    await scanSubscription?.cancel(); //termina cualquier escucha previa
    dispositivosEncontrados.clear(); //borra dispositivos de escaneos anteriores

    scanSubscription = FlutterBluePlus.onScanResults.listen((resultados) {
      //listen se activa solo cuando hay algo

      for (final resultado in resultados) {
        //itera en los resultados obtenidos

        final id = resultado.device.remoteId
            .toString(); //resultado.device = dispositivo; remoteId es el identificador
        dispositivosEncontrados[id] = resultado;
        //agrega/actualiza el dispositivo al map
      }
      setState(() {});
    });

    estado = "Escaneando dispositivos BLE...";
    setState(() {});

    await FlutterBluePlus.startScan(
      //el await hace que flutter espere a que flutter termine de mandar la orden de escaneo
      timeout: const Duration(seconds: 5), //"el escaneo debe durar 5 segundos
    );

    await FlutterBluePlus
        .isScanning //isScanning puede devolver true o false
        .where(
          (escaneando) => escaneando == false,
        ) //espera a que se escanee un falso
        .first; //toma el primer escaneo de falso
    estado = "Escaneo terminado";
    setState(() {});
  }

  Future<void> conectarDispositivo(ScanResult resultado) async {
    //para conectarse a dispositivos

    final dispositivo = resultado.device;
    //elije el "device" dentro de todos los datos que tiene

    estado = "Conectando...";
    setState(() {});

    try {
      //try-catch
      await FlutterBluePlus.stopScan(); //frenar el escaneo (jic)

      await dispositivo.connect(
        license: License.nonprofit, //proyecto escolar
        timeout: const Duration(seconds: 15), //si no llega va al catch
        autoConnect: false,
      );

      dispositivoConectado = dispositivo; //guardo en la variable global

      estado = "Conectado a ${resultado.advertisementData.advName}"; //aviso

      await descubrirServicios(dispositivo); //busca e imprime los servicios
      setState(() {});
    } catch (error) {
      //error es una variable
      estado = "Error al conectar: $error";
      setState(() {});
    }
  }

  Future<void> descubrirServicios(BluetoothDevice dispositivo) async {
    estado = "Buscando servicios...";
    setState(() {});

    final servicios = await dispositivo.discoverServices();

    for (final servicio in servicios) {
      if (servicio.uuid != serviceUuid) {
        continue;
        //saltea al siguiente servicio
      }
      for (final caracteristica in servicio.characteristics) {
        if (caracteristica.uuid == characteristicUuid) {
          caracteristicaDatos = caracteristica;

          estado = "Característica de datos encontrada";
          setState(() {});

          debugPrint("Característica correcta encontrada");
          debugPrint("READ: ${caracteristica.properties.read}");
          debugPrint("NOTIFY: ${caracteristica.properties.notify}");
        }
      }
    }
    setState(() {});
  }

  @override
  Widget build(BuildContext context) {
    final listaDispositivos = dispositivosEncontrados.values.toList();
    //necesitamos una lista para el ListBuilder, solo agarra los valores (no keys)

    listaDispositivos.sort(
      //ordena a los dispositivos por señal
      (a, b) => b.rssi.compareTo(a.rssi), //rssi = calidad de señal
    );

    return Scaffold(
      appBar: AppBar(
        title: Center(
          child: const Text(
            "Pulsera BLE",
            style: TextStyle(color: Colors.blueGrey),
          ),
        ),
      ),
      body: Padding(
        padding: const EdgeInsets.all(16), //16 píxeles de todas las esquinas
        child: Center(
          child: SizedBox(
            width: 600,
            child: Center(
              child: Column(
                mainAxisAlignment: MainAxisAlignment.center,
                crossAxisAlignment: CrossAxisAlignment.center,
                children: [
                  Text(
                    estado,
                    style: TextStyle(color: Colors.blueGrey, fontSize: 30),
                  ),
                  const SizedBox(height: 20),
                  ElevatedButton(
                    onPressed: iniciarEscaneo, //no hace falta usar (){}
                    child: const Text("Iniciar escaneo"),
                  ),
                  const SizedBox(height: 20),

                  Text(
                    "Encontrados: ${dispositivosEncontrados.length}",
                    style: TextStyle(color: Colors.blueGrey, fontSize: 40),
                  ),

                  Expanded(
                    //"usá todo el espacio que haya"
                    child: ListView.builder(
                      itemCount: listaDispositivos.length,
                      itemBuilder: (context, index) {
                        //iteracón

                        final resultado = listaDispositivos[index];

                        final nombre =
                            resultado.advertisementData.advName.isNotEmpty
                            ? resultado.advertisementData.advName
                            : "Dispositivo sin nombre";
                        //CONDICIONAL CORTO, busca si tiene nombre y lo usa

                        final id = resultado.device.remoteId.toString();
                        //obtiene el id

                        final senal = resultado.rssi;

                        return Card(
                          //hace que se vea junto
                          child: ListTile(
                            //crea la lista visual
                            leading: const Icon(Icons.bluetooth), //ícono
                            title: Text(nombre), //nombre del disp
                            subtitle: Text(
                              "ID: $id\nSeñal: $senal dBm",
                            ), //otros
                            isThreeLine:
                                true, //puede dar más de una línea de text}
                            trailing: ElevatedButton(
                              onPressed: () {
                                conectarDispositivo(resultado);
                              },
                              child: const Text("Conectar"),
                            ),
                          ),
                        );
                      },
                    ),
                  ),
                ],
              ),
            ),
          ),
        ),
      ),
    );
  }
}
