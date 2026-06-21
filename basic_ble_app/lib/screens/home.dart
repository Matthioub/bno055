import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';

class HomeScreen extends StatefulWidget {
  const HomeScreen({super.key});

  @override
  State<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen> {
  String estado = "Todavía no se escaneó";

  Future<void> iniciarEscaneo() async {
    //Future<void> = void; async permite usar await
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

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: Center(
          child: const Text(
            "Pulsera BLE",
            style: TextStyle(color: Colors.blueGrey),
          ),
        ),
      ),
      body: Center(
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            Text(estado, style: TextStyle(color: Colors.blueGrey)),
            const SizedBox(height: 20),
            ElevatedButton(
              onPressed: iniciarEscaneo, //no hace falta usar (){}
              child: const Text("Iniciar escaneo"),
            ),
          ],
        ),
      ),
    );
  }
}
