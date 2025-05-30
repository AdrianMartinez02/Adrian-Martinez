#include <stdio.h>

#define MAX_PRODUCTOS 5

// Estructura para productos
typedef struct {
    int id;
    char nombre[30];
    float precio;
} Producto;

int main() {
    Producto productos[MAX_PRODUCTOS] = {
        {1, "Martillo", 350.00},
        {2, "Destornillador", 150.00},
        {3, "Clavos (1 lb)", 100.00},
        {4, "Taladro", 2200.00},
        {5, "Cinta métrica", 180.00}
    };

    int opcion, codigo, cantidad;
    float total = 0.0;

    printf("=== Bienvenido a Ferretería Allmadera ===\n");

    do {
        printf("\nMenú:\n");
        printf("1. Ver productos disponibles\n");
        printf("2. Agregar producto al carrito\n");
        printf("3. Ver total a pagar\n");
        printf("4. Salir\n");
        printf("Selecciona una opción: ");
        scanf("%d", &opcion);

        switch(opcion) {
            case 1:
                printf("\n--- Lista de Productos ---\n");
                for (int i = 0; i < MAX_PRODUCTOS; i++) {
                    printf("%d. %s - RD$%.2f\n", productos[i].id, productos[i].nombre, productos[i].precio);
                }
                break;

            case 2:
                printf("\nIngresa el código del producto: ");
                scanf("%d", &codigo);
                if (codigo >= 1 && codigo <= MAX_PRODUCTOS) {
                    printf("Cantidad a agregar: ");
                    scanf("%d", &cantidad);
                    total += productos[codigo - 1].precio * cantidad;
                    printf("Producto agregado al carrito.\n");
                } else {
                    printf("Código de producto inválido.\n");
                }
                break;

            case 3:
                printf("\nTotal acumulado: RD$%.2f\n", total);
                break;

            case 4:
                printf("\nGracias por visitar Allmadera. ¡Vuelva pronto!\n");
                break;

            default:
                printf("Opción inválida, intenta de nuevo.\n");
        }

    } while (opcion != 4);

    return 0;
}


